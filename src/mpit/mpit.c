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

#include "names_ompi.h"

typedef struct {
    int index;
    int disp[16];      /* byte offsets of the elements */
    int nel;
    int src;           /* event source index */
} evinfo_t;

static evinfo_t ev[EV_N];
static int created_idx = -1, freed_idx = -1;
int edumpi_mpit_req_on;
static int cb_nop;                   /* EDUMPI_CB_NOP=1: callbacks return at once (measures the delivery cost of the library alone) */
static unsigned ev_mask = ~0u;       /* which event types are registered on communicators (EDUMPI_EVENT_MASK, bit = enum value) */
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

/* requests of user point-to-point calls that are in flight: token -> record and activation time (event ticks) */
typedef struct { uint64_t token; edumpi_rec_t *rec; int64_t t_act; int got_transfer; } act_t;
#define ACT_SLOTS 4096
static act_t acts[ACT_SLOTS];
static pthread_mutex_t act_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int n_acts;          /* live entries: lets match/transfer/complete events skip the lock when 0 */

static act_t *act_find(uint64_t token, int create)
{
    size_t h = (token >> 4) * 11400714819323198485ull % ACT_SLOTS, del = ACT_SLOTS;
    for (size_t i = 0; i < ACT_SLOTS; ++i) {
        size_t j = (h + i) % ACT_SLOTS;
        if (acts[j].token == token) {
            return &acts[j];
        }
        if (1 == acts[j].token && del == ACT_SLOTS) {
            del = j;
        }
        if (0 == acts[j].token) {
            return create ? &acts[del < ACT_SLOTS ? del : j] : NULL;
        }
    }
    return create && del < ACT_SLOTS ? &acts[del] : NULL;
}

static inline uint64_t u64at(const unsigned char *b, int off) { uint64_t v; memcpy(&v, b + off, 8); return v; }
static inline int64_t i64at(const unsigned char *b, int off) { int64_t v; memcpy(&v, b + off, 8); return v; }
static inline int32_t i32at(const unsigned char *b, int off) { int32_t v; memcpy(&v, b + off, 4); return v; }


/* ---- attribution of internal messages to collective records ----
 * A message with a parent token belongs to the non-blocking collective request with that token; one without belongs
 * to the blocking collective the calling thread is in. Records of non-blocking collectives can be written from the
 * thread that progresses the request, so these updates are serialised. */
static pthread_mutex_t coll_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct { uint64_t token; uint64_t parent; } rparent_t;     /* internal receive -> collective request */
#define RP_SLOTS 1024
static rparent_t rparents[RP_SLOTS];

static void rp_put(uint64_t token, uint64_t parent)
{
    size_t h = (token >> 4) % RP_SLOTS;
    for (size_t i = 0; i < RP_SLOTS; ++i) {
        rparent_t *e = &rparents[(h + i) % RP_SLOTS];
        if (0 == e->token || 1 == e->token || token == e->token) {
            e->token = token;
            e->parent = parent;
            return;
        }
    }
}

static uint64_t rp_take(uint64_t token)
{
    size_t h = (token >> 4) % RP_SLOTS;
    for (size_t i = 0; i < RP_SLOTS; ++i) {
        rparent_t *e = &rparents[(h + i) % RP_SLOTS];
        if (token == e->token) {
            e->token = 1;
            return e->parent;
        }
        if (0 == e->token) {
            break;
        }
    }
    return 0;
}

/* the record a message of this parent belongs to: the request's, else the current call's if it is a collective */
static edumpi_rec_t *coll_target(uint64_t parent, edumpi_rec_t *cur)
{
    edumpi_rec_t *r = parent ? edumpi_req_rec((uintptr_t) parent) : NULL;
    if (!r && cur && (cur->flags & EDUMPI_F_COLLECTIVE)) {
        r = cur;
    }
    return r;
}

static void sums_start(edumpi_rec_t *r)
{
    if (!(r->flags & EDUMPI_F_MPIT_SUM)) {     /* first internal message: drop the byte counts computed from the arguments */
        r->send_bytes = 0;
        r->recv_bytes = 0;
        r->flags |= EDUMPI_F_MPIT_SUM;
    }
}

static void coll_add_send(edumpi_rec_t *r, int64_t len, int pw)
{
    sums_start(r);
    r->send_bytes += len;
    if (pw >= 0) {
        unsigned words = (unsigned) (pw / 64) + 1;
        if (r->partner_bits_words < words) {
            uint64_t *nb = realloc(r->partner_bits, words * 8);
            if (nb) {
                memset(nb + r->partner_bits_words, 0, (words - r->partner_bits_words) * 8);
                r->partner_bits = nb;
                r->partner_bits_words = words;
            }
        }
        if (r->partner_bits_words >= words) {
            r->partner_bits[pw / 64] |= 1ull << (pw % 64);
        }
    }
}

static void coll_add_recv(edumpi_rec_t *r, int64_t len)
{
    sums_start(r);
    r->recv_bytes += len;
}

static void cb_pml(MPI_T_event_instance inst, MPI_T_event_registration reg, MPI_T_cb_safety safety, void *user)
{
    int kind = (int) (intptr_t) user;
    unsigned char b[256];
    const evinfo_t *e = &ev[kind];
    MPI_Count ts = 0;
    edumpi_rec_t *cur = edumpi_tls_cur;
    (void) reg;
    (void) safety;

    if (cb_nop) {
        return;
    }
    if (MPI_SUCCESS != MPI_T_event_copy(inst, b)) {
        return;
    }
    __sync_fetch_and_add(&n_events[kind], 1);
    if (EV_SEND_START == kind || EV_RECV_START == kind) {
        /* request, parent, length, peer, peer_world, tag, flags */
        int tag = i32at(b, e->disp[5]);
        uint64_t parent = u64at(b, e->disp[1]);
        if (tag >= 0) {
            return;
        }
        if (EV_RECV_START == kind) {
            if (parent) {                              /* completion comes later and carries no parent */
                pthread_mutex_lock(&coll_lock);
                rp_put(u64at(b, e->disp[0]), parent);
                pthread_mutex_unlock(&coll_lock);
            }
            return;
        }
        {
            edumpi_rec_t *r;
            int pw = i32at(b, e->disp[4]);
            __sync_fetch_and_add(&n_internal_send, 1);
            if (parent) {
                pthread_mutex_lock(&coll_lock);
                if ((r = coll_target(parent, cur)) != NULL) {
                    coll_add_send(r, i64at(b, e->disp[2]), pw);
                }
                pthread_mutex_unlock(&coll_lock);
            } else if (cur && (cur->flags & EDUMPI_F_COLLECTIVE)) {
                coll_add_send(cur, i64at(b, e->disp[2]), pw);
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
            if (is_recv) {
                uint64_t parent = 0;
                edumpi_rec_t *r = NULL;
                pthread_mutex_lock(&coll_lock);
                if (token) {
                    parent = rp_take(token);
                }
                if (parent && (r = coll_target(parent, cur)) != NULL) {
                    __sync_fetch_and_add(&n_internal_recv, 1);
                    coll_add_recv(r, len);
                    r = NULL;
                    parent = 1;
                }
                pthread_mutex_unlock(&coll_lock);
                if (1 != parent && cur && (cur->flags & EDUMPI_F_COLLECTIVE)) {
                    __sync_fetch_and_add(&n_internal_recv, 1);
                    coll_add_recv(cur, len);
                }
            }
            return;
        }
        if (token > 1 && n_acts > 0) {
            act_t *a;
            pthread_mutex_lock(&act_lock);
            if ((a = act_find(token, 0)) != NULL) {
                a->token = 1;                               /* request finished: forget it */
                n_acts--;
            }
            pthread_mutex_unlock(&act_lock);
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
            if (cur->partner < 0) {
                cur->partner = pw;          /* MPI_ANY_SOURCE: the sender; Sendrecv keeps its destination (as the fork) */
            }
            cur->recv_bytes = len;
            cur->flags |= EDUMPI_F_HAS_DONE | EDUMPI_F_EVENT_DONE;
        }
        return;
    }
    if (EV_ACTIVATE == kind || EV_MATCH == kind || EV_TRANSFER == kind) {
        uint64_t token = u64at(b, e->disp[0]);
        act_t *a;
        if (EV_ACTIVATE != kind && 0 == n_acts) {
            return;                                          /* nothing in flight that could be late */
        }
        MPI_T_event_get_timestamp(inst, &ts);
        pthread_mutex_lock(&act_lock);
        if (EV_ACTIVATE == kind) {
            if (token > 1 && (a = act_find(token, 0)) != NULL) {
                a->token = 1;       /* request structures are recycled: drop a stale entry of an earlier use */
                n_acts--;
            }
            if (cur && !(cur->flags & EDUMPI_F_COLLECTIVE) && token > 1 && (a = act_find(token, 1))) {
                a->token = token;
                n_acts++;
                a->rec = cur;
                a->t_act = (int64_t) ts;
                a->got_transfer = 0;
            }
        } else if ((a = act_find(token, 0)) != NULL) {
            if (EV_MATCH == kind && 0 == a->rec->late_sender_ns) {
                /* receive request matched by an arriving message: the receiver waited since the activation */
                a->rec->late_sender_ns = (uint64_t) ((int64_t) ts - a->t_act);
            } else if (EV_TRANSFER == kind && !a->got_transfer) {
                /* first data step after the activation: the sender waited for the receiver (rendezvous) */
                a->got_transfer = 1;
                a->rec->late_receiver_ns = (uint64_t) ((int64_t) ts - a->t_act);
            }
        }
        pthread_mutex_unlock(&act_lock);
        return;
    }
    if (EV_REQ_COMPLETED == kind) {
        /* set REQUEST: request, comm, kind, flags. Only collective requests are of interest here: the point-to-point
           requests report their completion with the envelope events. */
        uint64_t token = u64at(b, e->disp[0]);
        int rkind = i32at(b, e->disp[2]);
        edumpi_rec_t *r;
        if (REQ_KIND_COLL != rkind) {
            return;
        }
        MPI_T_event_get_timestamp(inst, &ts);
        pthread_mutex_lock(&coll_lock);
        r = coll_target(token, cur);
        if (r && !(r->flags & EDUMPI_F_EVENT_DONE) && !(r->flags & EDUMPI_F_BLOCKING)) {
            r->done_ns = (uint64_t) ((int64_t) ts + clock_offset_ns);
            r->flags |= EDUMPI_F_HAS_DONE | EDUMPI_F_EVENT_DONE;
        }
        pthread_mutex_unlock(&coll_lock);
        return;
    }
    if (EV_ALGORITHM == kind) {
        /* request, msg_size, parent_comm, operation, algorithm, component, comm_size, root, level */
        int op = i32at(b, e->disp[3]), alg = i32at(b, e->disp[4]), comp = i32at(b, e->disp[5]);
        int level = i32at(b, e->disp[8]);
        uint64_t alg_token = u64at(b, e->disp[0]);
        if (alg_token && edumpi_tls_pinit && level >= 0 && level <= 2) {      /* a persistent collective: the algorithm is chosen at *_init */
            edumpi_persist_set_alg((uintptr_t) alg_token, level, ((uint32_t) comp << 16) | (uint32_t) (alg & 0xFFFF));
        }
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

static void reg_one(commreg_t *slot, MPI_Comm *comm, int k)
{
    MPI_T_event_registration r;
    if (slot->n >= EV_N || ev[k].index < 0) {
        return;
    }
    if (MPI_SUCCESS == MPI_T_event_handle_alloc(ev[k].index, comm, MPI_INFO_NULL, &r)) {
        if (MPI_SUCCESS == MPI_T_event_register_callback(r, MPI_T_CB_REQUIRE_NONE, MPI_INFO_NULL,
                                                         (void *) (intptr_t) k, cb_pml)) {
            slot->reg[slot->n++] = r;
            __sync_fetch_and_add(&n_registered, 1);
        } else {
            MPI_T_event_handle_free(r, NULL, NULL);
        }
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
            if (ev_mask & (1u << k)) {
                reg_one(slot, &comm, k);
            }
        }
    }
    pthread_mutex_unlock(&reg_lock);
}

/* (flag declared at the top)
   The generic REQUEST_COMPLETED event is raised for every point-to-point request as well and costs 1 to 1.5 microseconds
   per message, but the tool needs it only for the completion time of non-blocking and persistent collectives. It is
   therefore registered when the program first calls such a collective (unless EDUMPI_EVENT_MASK says otherwise). */
void edumpi_mpit_need_coll_requests(void)
{
    if (!edumpi_mpit_active || edumpi_mpit_req_on) {
        return;
    }
    pthread_mutex_lock(&reg_lock);
    if (!edumpi_mpit_req_on) {
        if (!(ev_mask & (1u << EV_REQ_COMPLETED))) {
            ev_mask |= 1u << EV_REQ_COMPLETED;
            for (int i = 0; i < MAXREG; ++i) {
                if (regs[i].comm) {
                    MPI_Comm c = (MPI_Comm) (uintptr_t) regs[i].comm;
                    reg_one(&regs[i], &c, EV_REQ_COMPLETED);
                }
            }
        }
        edumpi_mpit_req_on = 1;
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


/* ---------- algorithm names: from the enumerations of the library's control variables ---------- */
static const char *comp_names[] = {"other", "basic", "tuned", "han", "libnbc", "adapt", "xhc", "acoll", "ucc", "sm", "self", "inter"};
static const char *op_names[] = {"allgather", "allgatherv", "allreduce", "alltoall", "alltoallv", "alltoallw", "barrier", "bcast",
                                 "exscan", "gather", "gatherv", "reduce", "reduce_scatter", "reduce_scatter_block", "scan",
                                 "scatter", "scatterv"};
#define N_OPS 17
#define MAX_ALG 32
static char alg_names[2][N_OPS][MAX_ALG][24];     /* [0] tuned, [1] libnbc */

static void build_alg_names(void)
{
    for (int c = 0; c < 2; ++c) {
        for (int o = 0; o < N_OPS; ++o) {
            char var[96], nm[128], desc[256];
            int idx, nl = sizeof(nm), dl = sizeof(desc), verb, bind, scope, num = 0, el = 0;
            MPI_Datatype dt;
            MPI_T_enum en;
            if (0 == c) {
                snprintf(var, sizeof(var), "coll_tuned_%s_algorithm", op_names[o]);
            } else {
                snprintf(var, sizeof(var), "coll_libnbc_i%s_algorithm", op_names[o]);
            }
            if (MPI_SUCCESS != MPI_T_cvar_get_index(var, &idx)
                || MPI_SUCCESS != MPI_T_cvar_get_info(idx, nm, &nl, &verb, &dt, &en, desc, &dl, &bind, &scope)
                || MPI_T_ENUM_NULL == en || MPI_SUCCESS != MPI_T_enum_get_info(en, &num, NULL, &el)) {
                continue;
            }
            for (int i = 0; i < num; ++i) {
                int value, il = 24;
                char item[24];
                if (MPI_SUCCESS == MPI_T_enum_get_item(en, i, &value, item, &il) && value >= 0 && value < MAX_ALG) {
                    snprintf(alg_names[c][o][value], sizeof(alg_names[c][o][value]), "%s", item);
                }
            }
        }
    }
}

static void alg_text(char *buf, size_t n, uint32_t packed, uint32_t op)
{
    unsigned comp = packed >> 16, id = packed & 0xFFFF;
    const char *name = NULL;
    char tmp[32];
    if (0 == packed) {
        snprintf(buf, n, "-");
        return;
    }
    if ((2 == comp || 4 == comp) && op < N_OPS && id < MAX_ALG && alg_names[2 == comp ? 0 : 1][op][id][0]) {
        name = alg_names[2 == comp ? 0 : 1][op][id];
    } else if (1 == comp) {
        name = id == 1 ? "linear" : id == 2 ? "log" : "inter";
    } else if (3 == comp) {
        name = 255 == id ? "simple" : 0 == id ? "task" : NULL;
    }
    if (!name) {
        snprintf(tmp, sizeof(tmp), "%u", id);
        name = tmp;
    }
    snprintf(buf, n, "%s %s", comp < sizeof(comp_names) / sizeof(comp_names[0]) ? comp_names[comp] : "?", name);
}

static size_t algorithm_text(const edumpi_rec_t *r, char *buf, size_t n)
{
    char a[48], b[48], c[48];
    if (!r->coll_alg[0] && !r->coll_alg[1] && !r->coll_alg[2]) {
        buf[0] = 0;
        return 0;
    }
    alg_text(a, sizeof(a), r->coll_alg[0], r->coll_op);
    if ((r->coll_alg[0] >> 16) == 3) {      /* HAN: the algorithms on the inter-node and intra-node communicators */
        alg_text(b, sizeof(b), r->coll_alg[1], r->coll_op);
        alg_text(c, sizeof(c), r->coll_alg[2], r->coll_op);
        return (size_t) snprintf(buf, n, "HAN(inter: %s, intra: %s)", b, c);
    }
    return (size_t) snprintf(buf, n, "%s", a);
}


void edumpi_mpit_init(void)
{
    int provided = 0;
    int ok = 1;
    const char *off = getenv("EDUMPI_MPIT");
    evinfo_t a, b;

    if (off && '0' == off[0]) {
        return;
    }
    ev_mask = ~0u;
    cb_nop = getenv("EDUMPI_CB_NOP") && '1' == getenv("EDUMPI_CB_NOP")[0];
    ev_mask &= ~(1u << EV_REQ_COMPLETED);               /* lazily, see edumpi_mpit_need_coll_requests() */
    if (getenv("EDUMPI_EVENT_MASK")) {
        ev_mask = (unsigned) strtoul(getenv("EDUMPI_EVENT_MASK"), NULL, 0);
        edumpi_mpit_req_on = 1;                          /* an explicit mask is taken as it is */
    }
    if (getenv("EDUMPI_MATCHING") && '0' == getenv("EDUMPI_MATCHING")[0]) {
        ev_mask &= ~((1u << EV_ACTIVATE) | (1u << EV_MATCH) | (1u << EV_TRANSFER));
    }
    /* The thread level of MPI_T is a request to the library: MULTIPLE can make the whole library use thread-safe
       code paths, which costs on every message. Default: SINGLE; EDUMPI_MPIT_THREADS=1 for programs that create
       communicators from several threads. */
    if (MPI_SUCCESS != MPI_T_init_thread(getenv("EDUMPI_MPIT_THREADS") ? MPI_THREAD_MULTIPLE : MPI_THREAD_SINGLE, &provided)) {
        return;
    }
    t_initialized = 1;
    for (int k = 0; k < EV_N; ++k) {
        ev[k].index = -1;
        if (0 != lookup(ev_table[k].impl, &ev[k]) || ev[k].nel != ev_table[k].elements) {
            ev[k].index = -1;
            if (ev_table[k].required) {
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
    if (getenv("EDUMPI_DEBUG")) {
        for (int k = 0; k < EV_N; ++k) {
            fprintf(stderr, "edumpi mpit: %-24s -> %-32s %s\n", ev_table[k].semantic, ev_table[k].impl,
                    ev[k].index >= 0 ? "found" : "not available");
        }
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
    build_alg_names();
    edumpi_algorithm_text = algorithm_text;
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
