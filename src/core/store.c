#define _GNU_SOURCE
#include "store.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
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

/* ---------- clock ----------
 * clock_gettime costs 1.3 us on nodes whose clock source is HPET (the cluster here), and a wrapped call needs two
 * stamps. On x86 with an invariant TSC the time is taken from the TSC and mapped to CLOCK_REALTIME with an anchor
 * (tsc0, ns0, ns per tick) that the monitor thread renews every flush, so that drift stays below a few microseconds. */
static uint64_t realtime_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

#if defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
static volatile struct { uint64_t tsc0, ns0; double ns_per_tick; } anchor;
static _Atomic unsigned anchor_seq;      /* odd while the anchor is written (seqlock) */
static int tsc_ok;

static int has_invariant_tsc(void)
{
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000000u, &a, &b, &c, &d) || a < 0x80000007u) {
        return 0;
    }
    __cpuid(0x80000007u, a, b, c, d);
    return (d >> 8) & 1;
}

static void anchor_set(uint64_t tsc, uint64_t ns, double mult)
{
    unsigned s = atomic_load_explicit(&anchor_seq, memory_order_relaxed);
    atomic_store_explicit(&anchor_seq, s + 1, memory_order_release);
    anchor.tsc0 = tsc;
    anchor.ns0 = ns;
    anchor.ns_per_tick = mult;
    atomic_store_explicit(&anchor_seq, s + 2, memory_order_release);
}

/* first: calibrate over 20 ms; later: new anchor with the rate measured since the last one */
static void tsc_anchor_update(int first)
{
    uint64_t t0 = __rdtsc(), n0 = realtime_now(), t1, n1;
    if (first) {
        struct timespec d = {0, 20000000};
        nanosleep(&d, NULL);
        n1 = realtime_now();
        t1 = __rdtsc();
        anchor_set(t1, n1, (double) (n1 - n0) / (double) (t1 - t0));
    } else {
        double m = (double) (n0 - anchor.ns0) / (double) (t0 - anchor.tsc0);
        anchor_set(t0, n0, m > 0 ? m : anchor.ns_per_tick);
    }
}

uint64_t edumpi_now_ns(void)
{
    if (tsc_ok) {
        unsigned s1, s2;
        uint64_t ns;
        do {
            s1 = atomic_load_explicit(&anchor_seq, memory_order_acquire);
            ns = anchor.ns0 + (uint64_t) ((double) (__rdtsc() - anchor.tsc0) * anchor.ns_per_tick);
            s2 = atomic_load_explicit(&anchor_seq, memory_order_acquire);
        } while ((s1 & 1) || s1 != s2);
        return ns;
    }
    return realtime_now();
}

static void clock_init(void)
{
    if (has_invariant_tsc() && !getenv("EDUMPI_NO_TSC")) {
        tsc_anchor_update(1);
        tsc_ok = 1;
    }
}
static void clock_renew(void)
{
    if (tsc_ok) {
        tsc_anchor_update(0);
    }
}
#else
uint64_t edumpi_now_ns(void) { return realtime_now(); }
static void clock_init(void) {}
static void clock_renew(void) {}
#endif

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

const char *edumpi_func_name(uint16_t id) { return id < nfuncs ? func_names[id] : ""; }
const char *edumpi_comm_name(uint16_t id) { return id < ncomms ? comm_names[id] : ""; }
size_t (*edumpi_algorithm_text)(const edumpi_rec_t *r, char *buf, size_t n);
int edumpi_g_rank;
char edumpi_g_node[64];

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
        if (r->end_ns && r->end_ns < r->start_ns) {
            r->end_ns = r->start_ns;     /* anchor renewal can move the clock back by a few microseconds */
        }
        if ((r->flags & EDUMPI_F_HAS_DONE) && r->done_ns < r->start_ns) {
            r->done_ns = r->start_ns;
        }
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
        clock_renew();
        edumpi_store_flush(0);
        pthread_mutex_lock(&mon_lock);
    }
    pthread_mutex_unlock(&mon_lock);
    return NULL;
}

int edumpi_store_init(int rank, int world_size, const char *node, edumpi_sink_t *sink, unsigned flush_ms)
{
    g_sink = sink;
    edumpi_g_rank = rank;
    snprintf(edumpi_g_node, sizeof(edumpi_g_node), "%s", node ? node : "");
    if (flush_ms > 0) {
        g_flush_ms = flush_ms;
    }
    if (sink && 0 != sink->open(sink, rank, world_size, node)) {
        g_sink = NULL;
    }
    clock_init();
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
