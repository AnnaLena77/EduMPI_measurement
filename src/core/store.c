#define _GNU_SOURCE
#include "store.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#define SEG_BYTES ((size_t) EDUMPI_SEG_EVENTS * sizeof(edumpi_rec_t))
#define FLUSHED ((edumpi_rec_t *) 1)
#define MAX_NAMES 1024
#define NAME_LEN 64

static _Atomic(edumpi_rec_t *) seg_table[EDUMPI_MAX_SEGS];
static _Atomic uint64_t claimed;
static _Atomic uint64_t dropped;
static _Atomic uint64_t written;
static uint64_t scan_from;            /* first segment that may not be flushed; under flush_lock */
static pthread_mutex_t flush_lock = PTHREAD_MUTEX_INITIALIZER;

static edumpi_sink_t *g_sink;
static int g_active;

static pthread_t monitor;
static int monitor_started;
static pthread_mutex_t mon_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mon_cond = PTHREAD_COND_INITIALIZER;
static int mon_stop;
static unsigned g_flush_ms = 500;

static pthread_mutex_t names_lock = PTHREAD_MUTEX_INITIALIZER;
static char func_names[MAX_NAMES][NAME_LEN];
static char comm_names[MAX_NAMES][NAME_LEN];
static unsigned nfuncs, ncomms;

uint64_t edumpi_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static uint16_t intern(char (*table)[NAME_LEN], unsigned *count, const char *name)
{
    uint16_t id = 0xFFFF;
    pthread_mutex_lock(&names_lock);
    for (unsigned i = 0; i < *count; ++i) {
        if (0 == strncmp(table[i], name, NAME_LEN - 1)) {
            id = (uint16_t) i;
            break;
        }
    }
    if (0xFFFF == id && *count < MAX_NAMES) {
        strncpy(table[*count], name, NAME_LEN - 1);
        id = (uint16_t) (*count)++;
    }
    pthread_mutex_unlock(&names_lock);
    return id;
}

uint16_t edumpi_func_id(const char *name) { return intern(func_names, &nfuncs, name); }
uint16_t edumpi_comm_id(const char *label) { return intern(comm_names, &ncomms, label); }

static edumpi_rec_t *seg_get(uint64_t s)
{
    edumpi_rec_t *base = atomic_load_explicit(&seg_table[s], memory_order_acquire);
    if (NULL == base) {
        edumpi_rec_t *fresh = mmap(NULL, SEG_BYTES, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        edumpi_rec_t *expected = NULL;
        if (MAP_FAILED == (void *) fresh) {
            return NULL;
        }
        if (atomic_compare_exchange_strong_explicit(&seg_table[s], &expected, fresh,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            return fresh;
        }
        munmap(fresh, SEG_BYTES);
        base = expected;
    }
    return FLUSHED == base ? NULL : base;
}

edumpi_rec_t *edumpi_begin(uint16_t func, uint16_t comm, uint32_t flags)
{
    uint64_t idx, s;
    edumpi_rec_t *seg, *r;

    if (!g_active) {
        return NULL;
    }
    idx = atomic_fetch_add_explicit(&claimed, 1, memory_order_relaxed);
    s = idx / EDUMPI_SEG_EVENTS;
    if (s >= EDUMPI_MAX_SEGS || NULL == (seg = seg_get(s))) {
        atomic_fetch_add_explicit(&dropped, 1, memory_order_relaxed);
        return NULL;
    }
    r = seg + (idx % EDUMPI_SEG_EVENTS);
    r->func = func;
    r->comm = comm;
    r->flags = flags;
    r->seq = (uint32_t) idx;
    r->partner = -1;
    r->start_ns = edumpi_now_ns();
    atomic_store_explicit(&r->state, EDUMPI_S_OPEN, memory_order_release);
    return r;
}

void edumpi_complete(edumpi_rec_t *r)
{
    if (r) {
        atomic_store_explicit(&r->state, EDUMPI_S_COMPLETE, memory_order_release);
    }
}

static void flush_segment(uint64_t s, edumpi_rec_t *base, uint64_t n)
{
    if (g_sink && n > 0) {
        g_sink->write(g_sink, base, n);
    }
    for (uint64_t i = 0; i < n; ++i) {
        free(base[i].partner_bits);
    }
    atomic_fetch_add_explicit(&written, n, memory_order_relaxed);
    atomic_store_explicit(&seg_table[s], FLUSHED, memory_order_release);
    munmap(base, SEG_BYTES);
}

void edumpi_store_flush(int force)
{
    uint64_t total, last;

    pthread_mutex_lock(&flush_lock);
    total = atomic_load_explicit(&claimed, memory_order_acquire);
    if (total > (uint64_t) EDUMPI_MAX_SEGS * EDUMPI_SEG_EVENTS) {
        total = (uint64_t) EDUMPI_MAX_SEGS * EDUMPI_SEG_EVENTS;
    }
    last = force ? (total + EDUMPI_SEG_EVENTS - 1) / EDUMPI_SEG_EVENTS : total / EDUMPI_SEG_EVENTS;
    for (uint64_t s = scan_from; s < last; ++s) {
        edumpi_rec_t *base = atomic_load_explicit(&seg_table[s], memory_order_acquire);
        uint64_t n = total - s * EDUMPI_SEG_EVENTS;
        int complete = 1;

        if (NULL == base || FLUSHED == base) {
            continue;
        }
        if (n > EDUMPI_SEG_EVENTS) {
            n = EDUMPI_SEG_EVENTS;
        }
        for (uint64_t i = 0; i < n; ++i) {
            if (EDUMPI_S_COMPLETE != atomic_load_explicit(&base[i].state, memory_order_acquire)) {
                if (!force) {
                    complete = 0;
                    break;
                }
                base[i].flags |= EDUMPI_F_NOT_COMPLETED;
                if (0 == base[i].end_ns) {
                    base[i].flags |= EDUMPI_F_NO_END;
                }
            }
        }
        if (complete) {
            flush_segment(s, base, n);
        }
    }
    while (scan_from < EDUMPI_MAX_SEGS
           && FLUSHED == atomic_load_explicit(&seg_table[scan_from], memory_order_acquire)) {
        ++scan_from;
    }
    pthread_mutex_unlock(&flush_lock);
}

static void *monitor_main(void *arg)
{
    (void) arg;
    pthread_mutex_lock(&mon_lock);
    while (!mon_stop) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long) g_flush_ms * 1000000L;
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
        pthread_cond_timedwait(&mon_cond, &mon_lock, &ts);
        pthread_mutex_unlock(&mon_lock);
        edumpi_store_flush(0);
        pthread_mutex_lock(&mon_lock);
    }
    pthread_mutex_unlock(&mon_lock);
    return NULL;
}

int edumpi_store_init(int rank, int world_size, const char *node, edumpi_sink_t *sink, unsigned flush_ms)
{
    g_sink = sink;
    if (flush_ms > 0) {
        g_flush_ms = flush_ms;
    }
    if (sink && 0 != sink->open(sink, rank, world_size, node)) {
        g_sink = NULL;
    }
    g_active = 1;
    mon_stop = 0;
    monitor_started = (0 == pthread_create(&monitor, NULL, monitor_main, NULL));
    return 0;
}

void edumpi_store_finalize(void)
{
    const char *funcs[MAX_NAMES], *comms[MAX_NAMES];

    if (!g_active) {
        return;
    }
    if (monitor_started) {
        pthread_mutex_lock(&mon_lock);
        mon_stop = 1;
        pthread_cond_signal(&mon_cond);
        pthread_mutex_unlock(&mon_lock);
        pthread_join(monitor, NULL);
        monitor_started = 0;
    }
    edumpi_store_flush(1);
    g_active = 0;
    if (g_sink) {
        for (unsigned i = 0; i < nfuncs; ++i) {
            funcs[i] = func_names[i];
        }
        for (unsigned i = 0; i < ncomms; ++i) {
            comms[i] = comm_names[i];
        }
        g_sink->close(g_sink, funcs, nfuncs, comms, ncomms);
        g_sink = NULL;
    }
}

uint64_t edumpi_store_claimed(void) { return atomic_load(&claimed); }
uint64_t edumpi_store_dropped(void) { return atomic_load(&dropped); }
uint64_t edumpi_store_written(void) { return atomic_load(&written); }
