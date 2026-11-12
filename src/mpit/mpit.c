/*
 * MPI_T backend: finds the events of the library (names of Open MPI with the PML and
 * collective events of the EduMPI fork), registers them on every communicator and turns them
 * into record data:
 *
 *   pml_recv_complete / pml_send_complete  of a user message (tag >= 0)
 *       -> arrival or completion time, real sender and received bytes of the record whose
 *          request has this token (the token is the MPI_Request handle) or, for a blocking
 *          call, of the record of the current call
 *   pml_send_start / pml_recv_complete with a negative tag (messages Open MPI sends for a
 *       collective) while a blocking collective runs in this thread
 *       -> byte sums and the bitmap of world ranks the process sent to
 *   coll_algorithm_selected
 *       -> algorithm of the collective: record->coll_alg[level]
 *
 * Callbacks never call MPI. If an event is missing the tool runs on PMPI alone.
 */
#define _GNU_SOURCE
#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../pmpi/wrap.h"

enum { EV_SEND_START, EV_RECV_START, EV_SEND_COMPLETE, EV_RECV_COMPLETE, EV_ALGORITHM, EV_N };
static const char *ev_names[EV_N] = {"ompi.pml_send_start", "ompi.pml_recv_start", "ompi.pml_send_complete",
                                     "ompi.pml_recv_complete", "ompi.coll_algorithm_selected"};

typedef struct {
    int index;
    int disp[16];      /* byte offsets of the elements */
    int nel;
    int src;           /* event source index */
} evinfo_t;

static evinfo_t ev[EV_N];
static int created_idx = -1, freed_idx = -1;
static int64_t clock_offset_ns;      /* CLOCK_REALTIME - source ticks, ticks are ns */
static int t_initialized;

/* handles registered per communicator, freed when the communicator is freed */
typedef struct {
    uint64_t comm;
    MPI_T_event_registration reg[EV_N];
    int n;
} commreg_t;
#define MAXREG 4096
static commreg_t regs[MAXREG];
static pthread_mutex_t reg_lock = PTHREAD_MUTEX_INITIALIZER;
static MPI_T_event_registration created_reg, freed_reg;
static volatile unsigned long long n_events[EV_N], n_internal_send, n_internal_recv, n_registered, n_created;

static uint64_t realtime_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void calibrate(int src)
{
    int64_t best_gap = INT64_MAX;
    for (int i = 0; i < 7; ++i) {
        MPI_Count t = 0;
        uint64_t a = realtime_ns();
        MPI_T_source_get_timestamp(src, &t);
        uint64_t b = realtime_ns();
        if ((int64_t) (b - a) < best_gap) {
            best_gap = (int64_t) (b - a);
            clock_offset_ns = (int64_t) (a + (b - a) / 2) - (int64_t) t;
        }
    }
}

static inline uint64_t u64at(const unsigned char *b, int off) { uint64_t v; memcpy(&v, b + off, 8); return v; }
static inline int64_t i64at(const unsigned char *b, int off) { int64_t v; memcpy(&v, b + off, 8); return v; }
static inline int32_t i32at(const unsigned char *b, int off) { int32_t v; memcpy(&v, b + off, 4); return v; }

static void cb_pml(MPI_T_event_instance inst, MPI_T_event_registration reg, MPI_T_cb_safety safety, void *user)
{
    int kind = (int) (intptr_t) user;
    unsigned char b[256];
    const evinfo_t *e = &ev[kind];
    MPI_Count ts = 0;
    edumpi_rec_t *cur = edumpi_tls_cur;
    (void) reg;
    (void) safety;

    if (MPI_SUCCESS != MPI_T_event_copy(inst, b)) {
        return;
    }
    __sync_fetch_and_add(&n_events[kind], 1);
    if (EV_SEND_START == kind || EV_RECV_START == kind) {
        /* request, parent, length, peer, peer_world, tag, flags */
        int tag = i32at(b, e->disp[5]);
        if (EV_SEND_START == kind && tag < 0 && cur && (cur->flags & EDUMPI_F_COLLECTIVE)) {
            int pw = i32at(b, e->disp[4]);
            __sync_fetch_and_add(&n_internal_send, 1);
            cur->send_bytes += i64at(b, e->disp[2]);
            cur->flags |= EDUMPI_F_MPIT_SUM;
            if (pw >= 0) {
                unsigned words = (unsigned) (pw / 64) + 1;
                if (cur->partner_bits_words < words) {
                    uint64_t *nb = realloc(cur->partner_bits, words * 8);
                    if (nb) {
                        memset(nb + cur->partner_bits_words, 0, (words - cur->partner_bits_words) * 8);
                        cur->partner_bits = nb;
                        cur->partner_bits_words = words;
                    }
                }
                if (cur->partner_bits_words >= words) {
                    cur->partner_bits[pw / 64] |= 1ull << (pw % 64);
                }
            }
        }
        return;
    }
    if (EV_SEND_COMPLETE == kind || EV_RECV_COMPLETE == kind) {
        /* request, length, peer, peer_world, tag, flags */
        uint64_t token = u64at(b, e->disp[0]);
        int64_t len = i64at(b, e->disp[1]);
        int pw = i32at(b, e->disp[3]);
        int tag = i32at(b, e->disp[4]);
        int is_recv = (EV_RECV_COMPLETE == kind);

        if (tag < 0) {                                    /* internal message of a collective */
            if (is_recv && cur && (cur->flags & EDUMPI_F_COLLECTIVE)) {
                __sync_fetch_and_add(&n_internal_recv, 1);
                cur->recv_bytes += len;
                cur->flags |= EDUMPI_F_MPIT_SUM;
            }
            return;
        }
        MPI_T_event_get_timestamp(inst, &ts);
        uint64_t when = (uint64_t) ((int64_t) ts + clock_offset_ns);
        if (token && edumpi_req_fill((uintptr_t) token, when, pw, len, is_recv)) {
            return;
        }
        /* blocking call: the record of the current call (receive: arrival time, sender, bytes) */
        if (cur && !(cur->flags & EDUMPI_F_COLLECTIVE) && (cur->flags & EDUMPI_F_BLOCKING) && is_recv
            && !(cur->flags & EDUMPI_F_EVENT_DONE)) {
            cur->done_ns = when;
            cur->partner = pw;
            cur->recv_bytes = len;
            cur->flags |= EDUMPI_F_HAS_DONE | EDUMPI_F_EVENT_DONE;
        }
        return;
    }
    if (EV_ALGORITHM == kind) {
        /* request, msg_size, parent_comm, operation, algorithm, component, comm_size, root, level */
        int op = i32at(b, e->disp[3]), alg = i32at(b, e->disp[4]), comp = i32at(b, e->disp[5]);
        int level = i32at(b, e->disp[8]);
        if (level < 0 || level > 2 || NULL == cur || !(cur->flags & EDUMPI_F_COLLECTIVE)) {
            return;
        }
        if (0 == level && edumpi_tls_op >= 0 && op != edumpi_tls_op) {
            return;                                       /* an inner operation of the algorithm, not the call */
        }
        if (0 == level && cur->coll_alg[0]) {
            return;
        }
        cur->coll_alg[level] = ((uint32_t) comp << 16) | (uint32_t) (alg & 0xFFFF);
    }
}

static void register_comm(uint64_t handle)
{
    MPI_Comm comm = (MPI_Comm) (uintptr_t) handle;
    commreg_t *slot = NULL;

    pthread_mutex_lock(&reg_lock);
    for (int i = 0; i < MAXREG; ++i) {
        if (regs[i].comm == handle) {
            pthread_mutex_unlock(&reg_lock);
            return;
        }
        if (!slot && 0 == regs[i].comm) {
            slot = &regs[i];
        }
    }
    if (slot) {
        slot->comm = handle;
        slot->n = 0;
        for (int k = 0; k < EV_N; ++k) {
            MPI_T_event_registration r;
            if (ev[k].index < 0) {
                continue;
            }
            if (MPI_SUCCESS == MPI_T_event_handle_alloc(ev[k].index, &comm, MPI_INFO_NULL, &r)) {
                if (MPI_SUCCESS == MPI_T_event_register_callback(r, MPI_T_CB_REQUIRE_NONE, MPI_INFO_NULL,
                                                                 (void *) (intptr_t) k, cb_pml)) {
                    slot->reg[slot->n++] = r;
                    __sync_fetch_and_add(&n_registered, 1);
                } else {
                    MPI_T_event_handle_free(r, NULL, NULL);
                }
            }
        }
    }
    pthread_mutex_unlock(&reg_lock);
}

static void unregister_comm(uint64_t handle)
{
    pthread_mutex_lock(&reg_lock);
    for (int i = 0; i < MAXREG; ++i) {
        if (regs[i].comm == handle) {
            for (int k = 0; k < regs[i].n; ++k) {
                MPI_T_event_handle_free(regs[i].reg[k], NULL, NULL);
            }
            memset(&regs[i], 0, sizeof(regs[i]));
            break;
        }
    }
    pthread_mutex_unlock(&reg_lock);
}

static void cb_comm(MPI_T_event_instance inst, MPI_T_event_registration reg, MPI_T_cb_safety safety, void *user)
{
    uint64_t handle = 0;
    (void) reg;
    (void) safety;
    /* communicator_created: (context id i32, handle u64); communicator_freed: (handle u64) */
    if (MPI_SUCCESS != MPI_T_event_read(inst, user ? 1 : 0, &handle) || 0 == handle) {
        return;
    }
    if (user) {
        __sync_fetch_and_add(&n_created, 1);
        register_comm(handle);
    } else {
        unregister_comm(handle);
    }
}

/* index of the event source named "ompi" (nanosecond ordered clock of the library's events) */
static int ompi_source(void)
{
    int n = 0;
    if (MPI_SUCCESS != MPI_T_source_get_num(&n)) {
        return 0;
    }
    for (int i = 0; i < n; ++i) {
        char name[128], desc[128];
        int nl = sizeof(name), dl = sizeof(desc);
        MPI_T_source_order order;
        MPI_Count tps, maxts;
        MPI_Info info;
        if (MPI_SUCCESS == MPI_T_source_get_info(i, name, &nl, desc, &dl, &order, &tps, &maxts, &info)
            && 0 == strcmp(name, "ompi")) {
            return i;
        }
    }
    return 0;
}

static int lookup(const char *name, evinfo_t *out)
{
    int idx;
    char nm[256];
    int nlen, verb, nel = 16, bind, dlen = 0;
    MPI_Datatype types[16];
    MPI_Aint disp[16];
    MPI_T_enum en;
    MPI_Info info;

    if (MPI_SUCCESS != MPI_T_event_get_index(name, &idx)) {
        return -1;
    }
    nlen = sizeof(nm);
    if (MPI_SUCCESS != MPI_T_event_get_info(idx, nm, &nlen, &verb, types, disp, &nel, &en, &info, NULL, &dlen, &bind)) {
        return -1;
    }
    out->index = idx;
    out->nel = nel;
    for (int i = 0; i < nel && i < 16; ++i) {
        out->disp[i] = (int) disp[i];
    }
    out->src = ompi_source();
    return 0;
}

static const int need_elems[EV_N] = {7, 7, 6, 6, 9};

void edumpi_mpit_init(void)
{
    int provided = 0;
    int ok = 1;
    const char *off = getenv("EDUMPI_MPIT");
    evinfo_t a, b;

    if (off && '0' == off[0]) {
        return;
    }
    if (MPI_SUCCESS != MPI_T_init_thread(MPI_THREAD_MULTIPLE, &provided)) {
        return;
    }
    t_initialized = 1;
    for (int k = 0; k < EV_N; ++k) {
        ev[k].index = -1;
        if (0 != lookup(ev_names[k], &ev[k]) || ev[k].nel != need_elems[k]) {
            ev[k].index = -1;
            if (k == EV_RECV_COMPLETE || k == EV_SEND_START) {
                ok = 0;
            }
        }
    }
    if (!ok) {
        return;     /* stock library: PMPI only */
    }
    if (0 == lookup("ompi.mpi.communicator_created", &a) && 0 == lookup("ompi.mpi.communicator_freed", &b)) {
        created_idx = a.index;
        freed_idx = b.index;
    }
    calibrate(ev[EV_RECV_COMPLETE].src);
    if (created_idx >= 0
        && MPI_SUCCESS == MPI_T_event_handle_alloc(created_idx, NULL, MPI_INFO_NULL, &created_reg)) {
        MPI_T_event_register_callback(created_reg, MPI_T_CB_REQUIRE_NONE, MPI_INFO_NULL, (void *) 1, cb_comm);
    }
    if (freed_idx >= 0
        && MPI_SUCCESS == MPI_T_event_handle_alloc(freed_idx, NULL, MPI_INFO_NULL, &freed_reg)) {
        MPI_T_event_register_callback(freed_reg, MPI_T_CB_REQUIRE_NONE, MPI_INFO_NULL, NULL, cb_comm);
    }
    edumpi_mpit_active = 1;
}

void edumpi_mpit_after_init(void)
{
    if (!edumpi_mpit_active) {
        return;
    }
    register_comm((uint64_t) (uintptr_t) MPI_COMM_WORLD);
    register_comm((uint64_t) (uintptr_t) MPI_COMM_SELF);
}

void edumpi_mpit_finalize(void)
{
    if (!t_initialized) {
        return;
    }
    edumpi_mpit_active = 0;
    if (getenv("EDUMPI_DEBUG")) {
        fprintf(stderr, "edumpi mpit: communicators created %llu, registrations %llu, events send_start %llu recv_start %llu send_complete %llu recv_complete %llu algorithm %llu, internal sends %llu recvs %llu\n",
                n_created, n_registered, n_events[0], n_events[1], n_events[2], n_events[3], n_events[4], n_internal_send, n_internal_recv);
    }
    for (int i = 0; i < MAXREG; ++i) {
        if (regs[i].comm) {
            unregister_comm(regs[i].comm);
        }
    }
    MPI_T_finalize();
    t_initialized = 0;
}
