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
static _Atomic uint64_t flushed_upto; /* copy of scan_from for the readers without the lock */
static uint64_t g_mem_segs = 1024;    /* thread-less: write when this many segments (256 KB each) are held in memory */
static pthread_mutex_t flush_lock = PTHREAD_MUTEX_INITIALIZER;

static edumpi_sink_t *g_sink;
static int g_active;
static int g_threadless;              /* no monitor thread: the application threads flush and renew the clock */
_Static_assert(0 == (EDUMPI_SEG_EVENTS & (EDUMPI_SEG_EVENTS - 1)), "segment size must be a power of two");
static void inline_flush(void);

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
static atomic_flag renew_busy = ATOMIC_FLAG_INIT;   /* one writer of the anchor at a time */
/* Thread-less mode: ticks after which the application thread renews the anchor itself (about 1 s). UINT64_MAX when a monitor
   thread does it. Costs one compare per time stamp. */
static uint64_t renew_ticks = UINT64_MAX, renew_max = UINT64_MAX;

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

/* One (tsc, ns) pair. The clock is read between two TSC reads and the pair uses the middle of the two (best of three), so
   that the read latency (about 1 us on HPET) does not skew the rate; with the reads in sequence the first calibration
   had a rate error of about 1e-4. */
static void clock_sample(uint64_t *tsc, uint64_t *ns)
{
    uint64_t best = ~0ull, bt = 0, bn = 0;
    for (int i = 0; i < 3; ++i) {
        uint64_t a = __rdtsc(), n = realtime_now(), b = __rdtsc();
        if (b - a < best) {
            best = b - a;
            bt = a + (b - a) / 2;
            bn = n;
        }
    }
    *tsc = bt;
    *ns = bn;
}

/* first: calibrate over 20 ms; later: new anchor with the rate measured since the last one */
static void tsc_anchor_update(int first)
{
    uint64_t t0, n0, t1, n1;
    clock_sample(&t0, &n0);
    if (first) {
        struct timespec d = {0, 20000000};
        nanosleep(&d, NULL);
        clock_sample(&t1, &n1);
        anchor_set(t1, n1, (double) (n1 - n0) / (double) (t1 - t0));
    } else {
        double m = (double) (n0 - anchor.ns0) / (double) (t0 - anchor.tsc0);
        anchor_set(t0, n0, m > 0 ? m : anchor.ns_per_tick);
    }
}

static void clock_renew(void);

uint64_t edumpi_now_ns(void)
{
    if (tsc_ok) {
        unsigned s1, s2;
        uint64_t ns, ticks;
        do {
            s1 = atomic_load_explicit(&anchor_seq, memory_order_acquire);
            ticks = __rdtsc() - anchor.tsc0;
            ns = anchor.ns0 + (uint64_t) ((double) ticks * anchor.ns_per_tick);
            s2 = atomic_load_explicit(&anchor_seq, memory_order_acquire);
        } while ((s1 & 1) || s1 != s2);
        if (__builtin_expect(ticks > renew_ticks, 0)) {
            clock_renew();
        }
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
    if (tsc_ok && !atomic_flag_test_and_set_explicit(&renew_busy, memory_order_acquire)) {
        tsc_anchor_update(0);
        if (renew_max != UINT64_MAX && renew_ticks < renew_max) {      /* thread-less: 0.1 s, 0.2 s, ... up to 1 s */
            renew_ticks = renew_ticks * 2 < renew_max ? renew_ticks * 2 : renew_max;
        }
        atomic_flag_clear_explicit(&renew_busy, memory_order_release);
    }
}
static void clock_renew_inline(int on)
{
    renew_max = on && tsc_ok ? (uint64_t) (1e9 / anchor.ns_per_tick) : UINT64_MAX;
    renew_ticks = renew_max == UINT64_MAX ? UINT64_MAX : renew_max / 10;
}
#else
uint64_t edumpi_now_ns(void) { return realtime_now(); }
static void clock_init(void) {}
static void clock_renew(void) {}
static void clock_renew_inline(int on) { (void) on; }
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
    if (__builtin_expect(0 == (idx & (EDUMPI_SEG_EVENTS - 1)) && idx > 0 && g_threadless, 0)
        && s - atomic_load_explicit(&flushed_upto, memory_order_relaxed) >= g_mem_segs) {
        inline_flush();
    }
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

static uint32_t seg_lo[EDUMPI_MAX_SEGS];      /* first record of the segment that is not written yet; under flush_lock */
static uint32_t seg_done[EDUMPI_MAX_SEGS];    /* records of the segment written so far; under flush_lock */

/* writes the records [a, b) of a segment to the sink and marks them written */
static void write_run(edumpi_rec_t *base, uint64_t a, uint64_t b)
{
    if (g_sink && b > a) {
        g_sink->write(g_sink, base + a, b - a);
    }
    for (uint64_t i = a; i < b; ++i) {
        free(base[i].partner_bits);
        base[i].partner_bits = NULL;
        atomic_store_explicit(&base[i].state, EDUMPI_S_WRITTEN, memory_order_release);
    }
    atomic_fetch_add_explicit(&written, b - a, memory_order_relaxed);
}

/* Every flush writes all records that are complete, also those of the segment that is still being filled, so that a
   reader of the database sees them within one flush interval. A record that is still open (a posted receive, a call
   that has not returned) is skipped and written when it completes. force: write everything (finalize). */
static void flush_locked(int force)
{
    uint64_t total, last;
    int any = 0;

    total = atomic_load_explicit(&claimed, memory_order_acquire);
    if (total > (uint64_t) EDUMPI_MAX_SEGS * EDUMPI_SEG_EVENTS) {
        total = (uint64_t) EDUMPI_MAX_SEGS * EDUMPI_SEG_EVENTS;
    }
    last = (total + EDUMPI_SEG_EVENTS - 1) / EDUMPI_SEG_EVENTS;
    for (uint64_t s = scan_from; s < last; ++s) {
        edumpi_rec_t *base = atomic_load_explicit(&seg_table[s], memory_order_acquire);
        uint64_t n = total - s * EDUMPI_SEG_EVENTS, i;

        if (NULL == base || FLUSHED == base) {
            continue;
        }
        if (n > EDUMPI_SEG_EVENTS) {
            n = EDUMPI_SEG_EVENTS;
        }
        i = seg_lo[s];
        while (i < n) {
            uint64_t a, b;
            uint32_t st;
            while (i < n && EDUMPI_S_WRITTEN == atomic_load_explicit(&base[i].state, memory_order_acquire)) {
                ++i;
            }
            a = i;
            while (i < n) {
                st = atomic_load_explicit(&base[i].state, memory_order_acquire);
                if (EDUMPI_S_COMPLETE != st) {
                    if (!force || EDUMPI_S_WRITTEN == st) {
                        break;
                    }
                    base[i].flags |= EDUMPI_F_NOT_COMPLETED;
                    if (0 == base[i].end_ns) {
                        base[i].flags |= EDUMPI_F_NO_END;
                    }
                }
                ++i;
            }
            b = i;
            if (b > a) {
                write_run(base, a, b);
                seg_done[s] += (uint32_t) (b - a);
                any = 1;
            }
            if (i < n && !force) {
                ++i;                    /* skip the record that is not complete */
            }
        }
        while (seg_lo[s] < n && EDUMPI_S_WRITTEN == atomic_load_explicit(&base[seg_lo[s]].state, memory_order_acquire)) {
            ++seg_lo[s];
        }
        if (seg_done[s] == n && (n == EDUMPI_SEG_EVENTS || force)) {
            atomic_store_explicit(&seg_table[s], FLUSHED, memory_order_release);
            munmap(base, SEG_BYTES);
        }
    }
    while (scan_from < EDUMPI_MAX_SEGS
           && FLUSHED == atomic_load_explicit(&seg_table[scan_from], memory_order_acquire)) {
        ++scan_from;
    }
    atomic_store_explicit(&flushed_upto, scan_from, memory_order_relaxed);
    if (any && g_sink && g_sink->sync) {
        g_sink->sync(g_sink);
    }
}

void edumpi_store_flush(int force)
{
    pthread_mutex_lock(&flush_lock);
    flush_locked(force);
    pthread_mutex_unlock(&flush_lock);
}

/* Thread-less mode: the application thread that claims the first record of a new segment writes the older segments. A thread
   that does not get the lock continues at once (another thread is flushing). */
static void inline_flush(void)
{
    if (0 == pthread_mutex_trylock(&flush_lock)) {
        flush_locked(0);
        pthread_mutex_unlock(&flush_lock);
    }
}

void edumpi_store_threadless(int on) { g_threadless = on; }

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

static void store_atexit(void)
{
    if (g_active) {
        edumpi_algorithm_text = NULL;     /* MPI_T may be gone */
        edumpi_store_finalize();
    }
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
    if (getenv("EDUMPI_MEM_MB")) {            /* thread-less: memory the store may hold before the caller writes (0: every segment) */
        long mb = atol(getenv("EDUMPI_MEM_MB"));
        g_mem_segs = mb <= 0 ? 1 : (uint64_t) mb * 4;     /* 256 KB per segment */
    }
    clock_init();
    clock_renew_inline(g_threadless);
    g_active = 1;
    mon_stop = 0;
    if (!g_threadless) {
        monitor_started = (0 == pthread_create(&monitor, NULL, monitor_main, NULL));
    }
    atexit(store_atexit);       /* a program that never reaches MPI_Finalize still leaves its data */
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
