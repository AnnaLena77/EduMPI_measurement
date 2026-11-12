/*
 * Communicator and window caches (label, rank, size, translation to world ranks), the request
 * table and the wrappers of Wait/Test/Comm_free/Request_free. This is the PMPI-only way to
 * learn when a non-blocking operation finished: the time a wait or test returned. The MPI_T
 * backend (later milestones) replaces it by the completion events of the library.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wrap.h"

int edumpi_enabled;
int edumpi_mpit_active;
_Thread_local edumpi_rec_t *edumpi_tls_cur;
_Thread_local int edumpi_tls_op = -1;

/* ---------- datatype sizes ---------- */
/* Handles of communicators and datatypes can be reused after a free; every free bumps this epoch and the
   per-thread caches below are valid only for the epoch they were filled in. */
static _Atomic unsigned cache_epoch = 1;

typedef struct { MPI_Datatype dt; int size; unsigned epoch; } tcache_t;
static _Thread_local tcache_t tcache[8];

int64_t edumpi_tsize(MPI_Datatype dt)
{
    int sz = 0;
    unsigned ep = atomic_load_explicit(&cache_epoch, memory_order_relaxed);
    tcache_t *e = &tcache[((uintptr_t) dt >> 4) & 7];
    if (MPI_DATATYPE_NULL == dt) {
        return 0;
    }
    if (e->dt == dt && e->epoch == ep) {
        return e->size;
    }
    PMPI_Type_size(dt, &sz);
    e->dt = dt; e->size = sz; e->epoch = ep;
    return sz;
}

int MPI_Type_free(MPI_Datatype *dt)
{
    atomic_fetch_add(&cache_epoch, 1);
    return PMPI_Type_free(dt);
}

int64_t edumpi_vsum(const int *counts, int n, MPI_Datatype dt)
{
    int64_t s = 0;
    if (NULL == counts) {
        return 0;
    }
    for (int i = 0; i < n; ++i) {
        s += counts[i];
    }
    return s * edumpi_tsize(dt);
}

int64_t edumpi_wsum(const int *counts, const MPI_Datatype *types, int n)
{
    int64_t s = 0;
    if (NULL == counts || NULL == types) {
        return 0;
    }
    for (int i = 0; i < n; ++i) {
        s += (int64_t) counts[i] * edumpi_tsize(types[i]);
    }
    return s;
}

/* ---------- caches keyed by handle ---------- */
#define CACHE_SLOTS 4096
typedef struct {
    uintptr_t key;      /* 0: free, 1: deleted */
    uint16_t id;
    int rank, size;
    int *world;         /* world rank of each rank, filled on first use */
} cobj_t;

static cobj_t comms[CACHE_SLOTS], wins[CACHE_SLOTS];
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned comm_counter, win_counter;

static cobj_t *find_slot(cobj_t *tab, uintptr_t key, int create)
{
    size_t h = (key >> 4) * 11400714819323198485ull % CACHE_SLOTS, first_del = CACHE_SLOTS;
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        size_t j = (h + i) % CACHE_SLOTS;
        if (tab[j].key == key) {
            return &tab[j];
        }
        if (1 == tab[j].key && first_del == CACHE_SLOTS) {
            first_del = j;
        }
        if (0 == tab[j].key) {
            if (!create) {
                return NULL;
            }
            return &tab[first_del < CACHE_SLOTS ? first_del : j];
        }
    }
    return create && first_del < CACHE_SLOTS ? &tab[first_del] : NULL;
}

static cobj_t *comm_obj(MPI_Comm comm)
{
    cobj_t *c = find_slot(comms, (uintptr_t) comm, 1);
    if (c && c->key != (uintptr_t) comm) {
        char label[64] = "", name[MPI_MAX_OBJECT_NAME] = "";
        int len = 0, flag = 0, inter = 0;
        free(c->world);
        memset(c, 0, sizeof(*c));
        c->key = (uintptr_t) comm;
        PMPI_Comm_rank(comm, &c->rank);
        PMPI_Comm_size(comm, &c->size);
        PMPI_Comm_test_inter(comm, &inter);
        if (inter) {
            PMPI_Comm_remote_size(comm, &c->size);   /* peers of p2p calls are in the remote group */
        }
        PMPI_Comm_get_name(comm, name, &len);
        if (comm == MPI_COMM_WORLD) {
            snprintf(label, sizeof(label), "MPI_COMM_WORLD");
        } else if (comm == MPI_COMM_SELF) {
            snprintf(label, sizeof(label), "MPI_COMM_SELF");
        } else if (len > 0 && name[0]) {
            snprintf(label, sizeof(label), "%.63s", name);
        } else {
            snprintf(label, sizeof(label), "MPI COMM %u", ++comm_counter);
        }
        (void) flag;
        c->id = edumpi_comm_id(label);
    }
    return c;
}

static void fill_world(cobj_t *c, MPI_Group g)
{
    int n = 0, *in;
    MPI_Group wg;
    PMPI_Group_size(g, &n);
    in = malloc(sizeof(int) * (n ? n : 1));
    c->world = malloc(sizeof(int) * (n ? n : 1));
    if (!in || !c->world) {
        free(in);
        free(c->world);
        c->world = NULL;
        return;
    }
    for (int i = 0; i < n; ++i) {
        in[i] = i;
    }
    PMPI_Comm_group(MPI_COMM_WORLD, &wg);
    PMPI_Group_translate_ranks(g, n, in, wg, c->world);
    PMPI_Group_free(&wg);
    free(in);
}

typedef struct { MPI_Comm comm; unsigned epoch; uint16_t id; int rank, size; const int *world; } ccache_t;
static _Thread_local ccache_t ccache;

/* fills the per-thread entry from the shared cache (under the lock); valid until a communicator is freed */
static ccache_t *ccache_get(MPI_Comm comm)
{
    unsigned ep = atomic_load_explicit(&cache_epoch, memory_order_relaxed);
    if (ccache.comm == comm && ccache.epoch == ep) {
        return &ccache;
    }
    pthread_mutex_lock(&cache_lock);
    cobj_t *c = comm_obj(comm);
    if (!c) {
        pthread_mutex_unlock(&cache_lock);
        return NULL;
    }
    ccache.comm = comm; ccache.epoch = ep; ccache.id = c->id; ccache.rank = c->rank; ccache.size = c->size;
    ccache.world = c->world;
    pthread_mutex_unlock(&cache_lock);
    return &ccache;
}

int edumpi_crank(MPI_Comm comm)
{
    ccache_t *c = ccache_get(comm);
    return c ? c->rank : -1;
}


int edumpi_csize(MPI_Comm comm)
{
    ccache_t *c = ccache_get(comm);
    return c ? c->size : 0;
}


uint16_t edumpi_cid(MPI_Comm comm)
{
    uint16_t v = EDUMPI_NO_COMM;
    if (MPI_COMM_NULL == comm) {
        return v;
    }
    {
        ccache_t *c = ccache_get(comm);
        return c ? c->id : v;
    }
    pthread_mutex_lock(&cache_lock);
    cobj_t *c = comm_obj(comm);
    if (c) {
        v = c->id;
    }
    pthread_mutex_unlock(&cache_lock);
    return v;
}

int edumpi_world(MPI_Comm comm, int rank)
{
    int v = -1;
    if (rank < 0 || MPI_COMM_NULL == comm) {
        return -1;
    }
    {
        ccache_t *c = ccache_get(comm);
        if (c && c->world && rank < c->size) {
            return c->world[rank];
        }
    }
    pthread_mutex_lock(&cache_lock);
    cobj_t *c = comm_obj(comm);
    if (c && !c->world) {
        MPI_Group g;
        int inter = 0;
        PMPI_Comm_test_inter(comm, &inter);
        if (inter) {
            PMPI_Comm_remote_group(comm, &g);
        } else {
            PMPI_Comm_group(comm, &g);
        }
        fill_world(c, g);
        PMPI_Group_free(&g);
    }
    if (c && c->world && rank < c->size) {
        v = c->world[rank];
        ccache.world = c->world;      /* the thread cache now sees the translation table */
    }
    pthread_mutex_unlock(&cache_lock);
    return v;
}

uint16_t edumpi_wid(MPI_Win win)
{
    uint16_t v = EDUMPI_NO_COMM;
    pthread_mutex_lock(&cache_lock);
    cobj_t *c = find_slot(wins, (uintptr_t) win, 1);
    if (c && c->key != (uintptr_t) win) {
        char label[64], name[MPI_MAX_OBJECT_NAME] = "";
        int len = 0;
        free(c->world);
        memset(c, 0, sizeof(*c));
        c->key = (uintptr_t) win;
        PMPI_Win_get_name(win, name, &len);
        if (len > 0 && name[0]) {
            snprintf(label, sizeof(label), "%.63s", name);
        } else {
            snprintf(label, sizeof(label), "MPI WIN %u", ++win_counter);
        }
        c->id = edumpi_comm_id(label);
    }
    if (c) {
        v = c->id;
    }
    pthread_mutex_unlock(&cache_lock);
    return v;
}

int edumpi_wworld(MPI_Win win, int rank)
{
    int v = -1;
    if (rank < 0) {
        return -1;
    }
    (void) edumpi_wid(win);
    pthread_mutex_lock(&cache_lock);
    cobj_t *c = find_slot(wins, (uintptr_t) win, 0);
    if (c && !c->world) {
        MPI_Group g;
        PMPI_Win_get_group(win, &g);
        PMPI_Group_size(g, &c->size);
        fill_world(c, g);
        PMPI_Group_free(&g);
    }
    if (c && c->world && rank < c->size) {
        v = c->world[rank];
    }
    pthread_mutex_unlock(&cache_lock);
    return v;
}

int MPI_Comm_free(MPI_Comm *comm)
{
    atomic_fetch_add(&cache_epoch, 1);
    if (edumpi_enabled && comm && MPI_COMM_NULL != *comm) {
        pthread_mutex_lock(&cache_lock);
        cobj_t *c = find_slot(comms, (uintptr_t) *comm, 0);
        if (c) {
            free(c->world);
            memset(c, 0, sizeof(*c));
            c->key = 1;
        }
        pthread_mutex_unlock(&cache_lock);
    }
    return PMPI_Comm_free(comm);
}

int MPI_Win_free(MPI_Win *win)
{
    if (edumpi_enabled && win && MPI_WIN_NULL != *win) {
        pthread_mutex_lock(&cache_lock);
        cobj_t *c = find_slot(wins, (uintptr_t) *win, 0);
        if (c) {
            free(c->world);
            memset(c, 0, sizeof(*c));
            c->key = 1;
        }
        pthread_mutex_unlock(&cache_lock);
    }
    return PMPI_Win_free(win);
}

/* ---------- request table ----------
 * Lock-free open addressing: a slot is claimed with a CAS on the key (0 free, 1 deleted, 2 being filled), the other
 * fields are written and the key is published last with release semantics. */
typedef struct {
    _Atomic uintptr_t key;      /* MPI_Request handle */
    edumpi_rec_t *rec;
    MPI_Comm comm;
    int kind;
} reqent_t;

#define REQ_SLOTS (CACHE_SLOTS * 4)
static reqent_t reqs[REQ_SLOTS];
static pthread_mutex_t req_lock = PTHREAD_MUTEX_INITIALIZER;   /* only for the stash below */

static size_t req_hash(uintptr_t key) { return (key >> 4) * 11400714819323198485ull % REQ_SLOTS; }

/* find the slot holding key (acquire), NULL if none */
static reqent_t *req_find(uintptr_t key)
{
    size_t h = req_hash(key);
    for (size_t i = 0; i < REQ_SLOTS; ++i) {
        reqent_t *e = &reqs[(h + i) % REQ_SLOTS];
        uintptr_t k = atomic_load_explicit(&e->key, memory_order_acquire);
        if (k == key) {
            return e;
        }
        if (0 == k) {
            return NULL;
        }
    }
    return NULL;
}

static int req_insert(uintptr_t key, edumpi_rec_t *rec, MPI_Comm comm, int kind)
{
    size_t h = req_hash(key);
    for (size_t i = 0; i < REQ_SLOTS; ++i) {
        reqent_t *e = &reqs[(h + i) % REQ_SLOTS];
        uintptr_t k = atomic_load_explicit(&e->key, memory_order_relaxed);
        if (k == key) {                 /* stale entry of an earlier use of the same handle: replace it */
            e->rec = rec; e->comm = comm; e->kind = kind;
            return 1;
        }
        if ((0 == k || 1 == k) && atomic_compare_exchange_strong(&e->key, &k, (uintptr_t) 2)) {
            e->rec = rec; e->comm = comm; e->kind = kind;
            atomic_store_explicit(&e->key, key, memory_order_release);
            return 1;
        }
    }
    return 0;
}

/* completion events that arrived before the call that started the request had returned */
typedef struct { uintptr_t token; uint64_t when; int partner, is_recv; int64_t bytes; } stash_t;
static stash_t stash[256];
static unsigned stash_next;
static _Atomic int stash_count;

static void apply_fill(edumpi_rec_t *r, uint64_t when, int partner, int64_t bytes, int is_recv)
{
    if (!(r->flags & EDUMPI_F_EVENT_DONE)) {
        r->done_ns = when;
        r->flags |= EDUMPI_F_HAS_DONE | EDUMPI_F_EVENT_DONE;
        if (is_recv) {
            r->partner = partner;
            r->recv_bytes = bytes;
        }
    }
}

int edumpi_req_fill(uintptr_t token, uint64_t when_ns, int partner, int64_t bytes, int is_recv)
{
    reqent_t *e = req_find(token);
    if (e && e->rec) {
        apply_fill(e->rec, when_ns, partner, bytes, is_recv);
        return 1;
    }
    pthread_mutex_lock(&req_lock);
    {
        stash_t *s = &stash[stash_next++ % 256];
        if (0 == s->token) {
            atomic_fetch_add(&stash_count, 1);
        }
        s->token = token; s->when = when_ns; s->partner = partner; s->bytes = bytes; s->is_recv = is_recv;
    }
    pthread_mutex_unlock(&req_lock);
    return 0;
}

void edumpi_req_add(MPI_Request req, edumpi_rec_t *rec, MPI_Comm comm, int kind)
{
    if (MPI_REQUEST_NULL == req || NULL == rec) {
        edumpi_complete(rec);
        return;
    }
    rec->request_token = (uintptr_t) req;
    if (!req_insert((uintptr_t) req, rec, comm, kind)) {   /* table full: finish the record now */
        edumpi_complete(rec);
        return;
    }
    if (atomic_load_explicit(&stash_count, memory_order_relaxed) > 0) {   /* an event for this request came first */
        pthread_mutex_lock(&req_lock);
        for (unsigned i = 0; i < 256; ++i) {
            if (stash[i].token == (uintptr_t) req) {
                apply_fill(rec, stash[i].when, stash[i].partner, stash[i].bytes, stash[i].is_recv);
                stash[i].token = 0;
                atomic_fetch_sub(&stash_count, 1);
            }
        }
        pthread_mutex_unlock(&req_lock);
    }
}

void edumpi_finish_after_call(edumpi_rec_t *r, MPI_Comm comm, int win)
{
    (void) comm;
    (void) win;
    if (r) {
        r->end_ns = edumpi_now_ns();
    }
}

/* take the entry out of the table (returns 1 and copies it if found) */
typedef struct { uintptr_t key; edumpi_rec_t *rec; MPI_Comm comm; int kind; } reqcopy_t;

static int req_take(MPI_Request req, reqcopy_t *out)
{
    reqent_t *e;
    if (MPI_REQUEST_NULL == req || NULL == (e = req_find((uintptr_t) req))) {
        return 0;
    }
    out->key = (uintptr_t) req; out->rec = e->rec; out->comm = e->comm; out->kind = e->kind;
    atomic_store_explicit(&e->key, (uintptr_t) 1, memory_order_release);
    return 1;
}

static void req_drop(uintptr_t key)
{
    reqent_t *e = req_find(key);
    if (e) {
        atomic_store_explicit(&e->key, (uintptr_t) 1, memory_order_release);
    }
}

static void req_complete(const reqcopy_t *e, const MPI_Status *st, uint64_t when)
{
    edumpi_rec_t *r = e->rec;
    int from_event = (r->flags & EDUMPI_F_EVENT_DONE) != 0;
    if (!from_event) {
        r->done_ns = when;
        r->flags |= EDUMPI_F_HAS_DONE;
    }
    r->request_token = 0;
    if (!from_event && EDUMPI_REQ_RECV == e->kind && st) {
        int n = 0;
        if (MPI_ANY_SOURCE == r->partner || r->flags & EDUMPI_F_ANY_SOURCE) {
            r->flags |= EDUMPI_F_ANY_SOURCE;
        }
        r->partner = edumpi_world(e->comm, st->MPI_SOURCE);
        PMPI_Get_count(st, MPI_BYTE, &n);
        r->recv_bytes = n;
    }
    edumpi_complete(r);
}

int MPI_Wait(MPI_Request *request, MPI_Status *status)
{
    reqcopy_t e;
    int have = 0, rc;
    MPI_Status local;
    edumpi_rec_t *w = NULL;
    static uint16_t fid = 0xFFFF;

    if (!edumpi_enabled) {
        return PMPI_Wait(request, status);
    }
    w = edumpi_rec_begin("MPI_Wait", &fid, EDUMPI_NO_COMM, EDUMPI_F_BLOCKING);
    have = request && req_take(*request, &e);
    rc = PMPI_Wait(request, (status == MPI_STATUS_IGNORE && have) ? &local : status);
    if (have) {
        req_complete(&e, status == MPI_STATUS_IGNORE ? &local : status, edumpi_now_ns());
    }
    if (w) {
        w->end_ns = edumpi_now_ns();
        edumpi_complete(w);
    }
    return rc;
}

int MPI_Test(MPI_Request *request, int *flag, MPI_Status *status)
{
    reqcopy_t e;
    int have = 0, rc;
    MPI_Status local;
    reqent_t *p;

    if (!edumpi_enabled || NULL == request) {
        return PMPI_Test(request, flag, status);
    }
    if (MPI_REQUEST_NULL != *request && (p = req_find((uintptr_t) *request)) != NULL) {
        e.key = (uintptr_t) *request; e.rec = p->rec; e.comm = p->comm; e.kind = p->kind;
        have = 1;
    }
    rc = PMPI_Test(request, flag, (status == MPI_STATUS_IGNORE && have) ? &local : status);
    if (have && flag && *flag) {
        uint64_t t = edumpi_now_ns();
        req_drop(e.key);
        req_complete(&e, status == MPI_STATUS_IGNORE ? &local : status, t);
    }
    return rc;
}

/* arrays: remember the entries before the call (the handles become MPI_REQUEST_NULL); small counts need no heap */
#define SNAP_STACK 32
typedef struct { reqcopy_t stack[SNAP_STACK]; reqcopy_t *v; int heap; } snap_t;

static void snapshot(snap_t *s, int count, MPI_Request *reqs_in)
{
    s->heap = count > SNAP_STACK;
    s->v = s->heap ? calloc(count, sizeof(reqcopy_t)) : s->stack;
    if (!s->v) {
        return;
    }
    memset(s->v, 0, sizeof(reqcopy_t) * (size_t) count);
    for (int i = 0; i < count; ++i) {
        reqent_t *p = MPI_REQUEST_NULL == reqs_in[i] ? NULL : req_find((uintptr_t) reqs_in[i]);
        if (p) {
            s->v[i].key = (uintptr_t) reqs_in[i]; s->v[i].rec = p->rec; s->v[i].comm = p->comm; s->v[i].kind = p->kind;
        }
    }
}

static void snap_free(snap_t *s)
{
    if (s->heap) {
        free(s->v);
    }
}

static void finish_idx(snap_t *s, int i, const MPI_Status *st, uint64_t t)
{
    if (s->v && s->v[i].key > 1) {
        req_drop(s->v[i].key);
        req_complete(&s->v[i], st, t);
    }
}

int MPI_Waitall(int count, MPI_Request reqarr[], MPI_Status statuses[])
{
    snap_t s;
    int rc;
    MPI_Status stack_st[SNAP_STACK], *st = statuses;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Waitall(count, reqarr, statuses);
    }
    snapshot(&s, count, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = count <= SNAP_STACK ? stack_st : malloc(sizeof(MPI_Status) * count);
    }
    rc = PMPI_Waitall(count, reqarr, st ? st : statuses);
    if (st) {
        uint64_t t = edumpi_now_ns();
        for (int i = 0; i < count; ++i) {
            finish_idx(&s, i, &st[i], t);
        }
    }
    if (st != statuses && st != stack_st) {
        free(st);
    }
    snap_free(&s);
    return rc;
}

int MPI_Waitany(int count, MPI_Request reqarr[], int *index, MPI_Status *status)
{
    snap_t s;
    int rc;
    MPI_Status local;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Waitany(count, reqarr, index, status);
    }
    snapshot(&s, count, reqarr);
    rc = PMPI_Waitany(count, reqarr, index, status == MPI_STATUS_IGNORE ? &local : status);
    if (*index >= 0 && *index < count) {
        finish_idx(&s, *index, status == MPI_STATUS_IGNORE ? &local : status, edumpi_now_ns());
    }
    snap_free(&s);
    return rc;
}

int MPI_Waitsome(int incount, MPI_Request reqarr[], int *outcount, int indices[], MPI_Status statuses[])
{
    snap_t s;
    int rc;
    MPI_Status stack_st[SNAP_STACK], *st = statuses;
    if (!edumpi_enabled || incount <= 0) {
        return PMPI_Waitsome(incount, reqarr, outcount, indices, statuses);
    }
    snapshot(&s, incount, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = incount <= SNAP_STACK ? stack_st : malloc(sizeof(MPI_Status) * incount);
    }
    rc = PMPI_Waitsome(incount, reqarr, outcount, indices, st ? st : statuses);
    if (st && *outcount > 0) {
        uint64_t t = edumpi_now_ns();
        for (int k = 0; k < *outcount; ++k) {
            finish_idx(&s, indices[k], &st[k], t);
        }
    }
    if (st != statuses && st != stack_st) {
        free(st);
    }
    snap_free(&s);
    return rc;
}

int MPI_Testall(int count, MPI_Request reqarr[], int *flag, MPI_Status statuses[])
{
    snap_t s;
    int rc;
    MPI_Status stack_st[SNAP_STACK], *st = statuses;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Testall(count, reqarr, flag, statuses);
    }
    snapshot(&s, count, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = count <= SNAP_STACK ? stack_st : malloc(sizeof(MPI_Status) * count);
    }
    rc = PMPI_Testall(count, reqarr, flag, st ? st : statuses);
    if (*flag && st) {
        uint64_t t = edumpi_now_ns();
        for (int i = 0; i < count; ++i) {
            finish_idx(&s, i, &st[i], t);
        }
    }
    if (st != statuses && st != stack_st) {
        free(st);
    }
    snap_free(&s);
    return rc;
}

int MPI_Testany(int count, MPI_Request reqarr[], int *index, int *flag, MPI_Status *status)
{
    snap_t s;
    int rc;
    MPI_Status local;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Testany(count, reqarr, index, flag, status);
    }
    snapshot(&s, count, reqarr);
    rc = PMPI_Testany(count, reqarr, index, flag, status == MPI_STATUS_IGNORE ? &local : status);
    if (*flag && *index >= 0 && *index < count) {
        finish_idx(&s, *index, status == MPI_STATUS_IGNORE ? &local : status, edumpi_now_ns());
    }
    snap_free(&s);
    return rc;
}

int MPI_Testsome(int incount, MPI_Request reqarr[], int *outcount, int indices[], MPI_Status statuses[])
{
    snap_t s;
    int rc;
    MPI_Status stack_st[SNAP_STACK], *st = statuses;
    if (!edumpi_enabled || incount <= 0) {
        return PMPI_Testsome(incount, reqarr, outcount, indices, statuses);
    }
    snapshot(&s, incount, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = incount <= SNAP_STACK ? stack_st : malloc(sizeof(MPI_Status) * incount);
    }
    rc = PMPI_Testsome(incount, reqarr, outcount, indices, st ? st : statuses);
    if (st && *outcount > 0) {
        uint64_t t = edumpi_now_ns();
        for (int k = 0; k < *outcount; ++k) {
            finish_idx(&s, indices[k], &st[k], t);
        }
    }
    if (st != statuses && st != stack_st) {
        free(st);
    }
    snap_free(&s);
    return rc;
}

int MPI_Request_free(MPI_Request *request)
{
    reqcopy_t e;
    if (edumpi_enabled && request && req_take(*request, &e)) {
        e.rec->request_token = 0;
        e.rec->flags |= EDUMPI_F_NOT_COMPLETED;   /* freed without observing completion */
        edumpi_complete(e.rec);
    }
    return PMPI_Request_free(request);
}
