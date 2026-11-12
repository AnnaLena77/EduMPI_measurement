/*
 * Communicator and window caches (label, rank, size, translation to world ranks), the request
 * table and the wrappers of Wait/Test/Comm_free/Request_free. This is the PMPI-only way to
 * learn when a non-blocking operation finished: the time a wait or test returned. The MPI_T
 * backend (later milestones) replaces it by the completion events of the library.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wrap.h"

int edumpi_enabled;
int edumpi_mpit_active;
_Thread_local edumpi_rec_t *edumpi_tls_cur;
_Thread_local int edumpi_tls_op = -1;

/* ---------- datatype sizes ---------- */
int64_t edumpi_tsize(MPI_Datatype dt)
{
    int sz = 0;
    if (MPI_DATATYPE_NULL == dt) {
        return 0;
    }
    PMPI_Type_size(dt, &sz);
    return sz;
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

int edumpi_crank(MPI_Comm comm)
{
    int v;
    pthread_mutex_lock(&cache_lock);
    cobj_t *c = comm_obj(comm);
    v = c ? c->rank : -1;
    pthread_mutex_unlock(&cache_lock);
    return v;
}

int edumpi_csize(MPI_Comm comm)
{
    int v;
    pthread_mutex_lock(&cache_lock);
    cobj_t *c = comm_obj(comm);
    v = c ? c->size : 0;
    pthread_mutex_unlock(&cache_lock);
    return v;
}

uint16_t edumpi_cid(MPI_Comm comm)
{
    uint16_t v = EDUMPI_NO_COMM;
    if (MPI_COMM_NULL == comm) {
        return v;
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

/* ---------- request table ---------- */
typedef struct {
    uintptr_t key;      /* MPI_Request handle; 0 free, 1 deleted */
    edumpi_rec_t *rec;
    MPI_Comm comm;
    int kind;
} reqent_t;

static reqent_t reqs[CACHE_SLOTS * 4];
#define REQ_SLOTS (CACHE_SLOTS * 4)
static pthread_mutex_t req_lock = PTHREAD_MUTEX_INITIALIZER;

static reqent_t *req_slot(uintptr_t key, int create)
{
    size_t h = (key >> 4) * 11400714819323198485ull % REQ_SLOTS, first_del = REQ_SLOTS;
    for (size_t i = 0; i < REQ_SLOTS; ++i) {
        size_t j = (h + i) % REQ_SLOTS;
        if (reqs[j].key == key) {
            return &reqs[j];
        }
        if (1 == reqs[j].key && first_del == REQ_SLOTS) {
            first_del = j;
        }
        if (0 == reqs[j].key) {
            return create ? &reqs[first_del < REQ_SLOTS ? first_del : j] : NULL;
        }
    }
    return create && first_del < REQ_SLOTS ? &reqs[first_del] : NULL;
}

/* completion events that arrived before the call that started the request had returned */
typedef struct { uintptr_t token; uint64_t when; int partner, is_recv; int64_t bytes; } stash_t;
static stash_t stash[256];
static unsigned stash_next;

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
    reqent_t *e;
    int applied = 0;
    pthread_mutex_lock(&req_lock);
    e = req_slot(token, 0);
    if (e && e->rec) {
        apply_fill(e->rec, when_ns, partner, bytes, is_recv);
        applied = 1;
    } else {
        stash_t *s = &stash[stash_next++ % 256];
        s->token = token; s->when = when_ns; s->partner = partner; s->bytes = bytes; s->is_recv = is_recv;
    }
    pthread_mutex_unlock(&req_lock);
    return applied;
}

void edumpi_req_add(MPI_Request req, edumpi_rec_t *rec, MPI_Comm comm, int kind)
{
    reqent_t *e;
    if (MPI_REQUEST_NULL == req || NULL == rec) {
        edumpi_complete(rec);
        return;
    }
    pthread_mutex_lock(&req_lock);
    e = req_slot((uintptr_t) req, 1);
    if (e) {
        e->key = (uintptr_t) req;
        e->rec = rec;
        e->comm = comm;
        e->kind = kind;
        rec->request_token = (uintptr_t) req;
        for (unsigned i = 0; i < 256; ++i) {      /* an event for this request came first */
            if (stash[i].token == (uintptr_t) req) {
                apply_fill(rec, stash[i].when, stash[i].partner, stash[i].bytes, stash[i].is_recv);
                stash[i].token = 0;
            }
        }
    }
    pthread_mutex_unlock(&req_lock);
    if (!e) {   /* table full: finish the record now, without completion data */
        edumpi_complete(rec);
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
static int req_take(MPI_Request req, reqent_t *out)
{
    int found = 0;
    reqent_t *e;
    if (MPI_REQUEST_NULL == req) {
        return 0;
    }
    pthread_mutex_lock(&req_lock);
    e = req_slot((uintptr_t) req, 0);
    if (e) {
        *out = *e;
        e->key = 1;
        found = 1;
    }
    pthread_mutex_unlock(&req_lock);
    return found;
}

static void req_drop(uintptr_t key)
{
    reqent_t *e;
    pthread_mutex_lock(&req_lock);
    e = req_slot(key, 0);
    if (e) {
        e->key = 1;
    }
    pthread_mutex_unlock(&req_lock);
}

static void req_complete(const reqent_t *e, const MPI_Status *st, uint64_t when)
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
    reqent_t e;
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
    reqent_t e;
    int have = 0, rc;
    MPI_Status local;
    if (!edumpi_enabled || NULL == request) {
        return PMPI_Test(request, flag, status);
    }
    pthread_mutex_lock(&req_lock);
    {
        reqent_t *p = req_slot((uintptr_t) *request, 0);
        if (p) {
            e = *p;
            have = 1;
        }
    }
    pthread_mutex_unlock(&req_lock);
    rc = PMPI_Test(request, flag, (status == MPI_STATUS_IGNORE && have) ? &local : status);
    if (have && flag && *flag) {
        uint64_t t = edumpi_now_ns();
        req_drop(e.key);
        req_complete(&e, status == MPI_STATUS_IGNORE ? &local : status, t);
    }
    return rc;
}

/* arrays: remember the handles before the call (they become MPI_REQUEST_NULL) */
static reqent_t *snapshot(int count, MPI_Request *reqs_in)
{
    reqent_t *s = calloc(count > 0 ? count : 1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    pthread_mutex_lock(&req_lock);
    for (int i = 0; i < count; ++i) {
        reqent_t *p = MPI_REQUEST_NULL == reqs_in[i] ? NULL : req_slot((uintptr_t) reqs_in[i], 0);
        if (p) {
            s[i] = *p;
        }
    }
    pthread_mutex_unlock(&req_lock);
    return s;
}

static void finish_idx(reqent_t *s, int i, const MPI_Status *st, uint64_t t)
{
    if (s && s[i].key > 1) {
        req_drop(s[i].key);
        req_complete(&s[i], st, t);
    }
}

int MPI_Waitall(int count, MPI_Request reqarr[], MPI_Status statuses[])
{
    reqent_t *s;
    int rc;
    MPI_Status *st = statuses;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Waitall(count, reqarr, statuses);
    }
    s = snapshot(count, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = malloc(sizeof(MPI_Status) * count);
    }
    rc = PMPI_Waitall(count, reqarr, st ? st : statuses);
    {
        uint64_t t = edumpi_now_ns();
        for (int i = 0; i < count && st; ++i) {
            finish_idx(s, i, &st[i], t);
        }
    }
    if (st != statuses) {
        free(st);
    }
    free(s);
    return rc;
}

int MPI_Waitany(int count, MPI_Request reqarr[], int *index, MPI_Status *status)
{
    reqent_t *s;
    int rc;
    MPI_Status local;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Waitany(count, reqarr, index, status);
    }
    s = snapshot(count, reqarr);
    rc = PMPI_Waitany(count, reqarr, index, status == MPI_STATUS_IGNORE ? &local : status);
    if (*index >= 0 && *index < count) {
        finish_idx(s, *index, status == MPI_STATUS_IGNORE ? &local : status, edumpi_now_ns());
    }
    free(s);
    return rc;
}

int MPI_Waitsome(int incount, MPI_Request reqarr[], int *outcount, int indices[], MPI_Status statuses[])
{
    reqent_t *s;
    int rc;
    MPI_Status *st = statuses;
    if (!edumpi_enabled || incount <= 0) {
        return PMPI_Waitsome(incount, reqarr, outcount, indices, statuses);
    }
    s = snapshot(incount, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = malloc(sizeof(MPI_Status) * incount);
    }
    rc = PMPI_Waitsome(incount, reqarr, outcount, indices, st ? st : statuses);
    if (st && *outcount > 0) {
        uint64_t t = edumpi_now_ns();
        for (int k = 0; k < *outcount; ++k) {
            finish_idx(s, indices[k], &st[k], t);
        }
    }
    if (st != statuses) {
        free(st);
    }
    free(s);
    return rc;
}

int MPI_Testall(int count, MPI_Request reqarr[], int *flag, MPI_Status statuses[])
{
    reqent_t *s;
    int rc;
    MPI_Status *st = statuses;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Testall(count, reqarr, flag, statuses);
    }
    s = snapshot(count, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = malloc(sizeof(MPI_Status) * count);
    }
    rc = PMPI_Testall(count, reqarr, flag, st ? st : statuses);
    if (*flag && st) {
        uint64_t t = edumpi_now_ns();
        for (int i = 0; i < count; ++i) {
            finish_idx(s, i, &st[i], t);
        }
    }
    if (st != statuses) {
        free(st);
    }
    free(s);
    return rc;
}

int MPI_Testany(int count, MPI_Request reqarr[], int *index, int *flag, MPI_Status *status)
{
    reqent_t *s;
    int rc;
    MPI_Status local;
    if (!edumpi_enabled || count <= 0) {
        return PMPI_Testany(count, reqarr, index, flag, status);
    }
    s = snapshot(count, reqarr);
    rc = PMPI_Testany(count, reqarr, index, flag, status == MPI_STATUS_IGNORE ? &local : status);
    if (*flag && *index >= 0 && *index < count) {
        finish_idx(s, *index, status == MPI_STATUS_IGNORE ? &local : status, edumpi_now_ns());
    }
    free(s);
    return rc;
}

int MPI_Testsome(int incount, MPI_Request reqarr[], int *outcount, int indices[], MPI_Status statuses[])
{
    reqent_t *s;
    int rc;
    MPI_Status *st = statuses;
    if (!edumpi_enabled || incount <= 0) {
        return PMPI_Testsome(incount, reqarr, outcount, indices, statuses);
    }
    s = snapshot(incount, reqarr);
    if (statuses == MPI_STATUSES_IGNORE) {
        st = malloc(sizeof(MPI_Status) * incount);
    }
    rc = PMPI_Testsome(incount, reqarr, outcount, indices, st ? st : statuses);
    if (st && *outcount > 0) {
        uint64_t t = edumpi_now_ns();
        for (int k = 0; k < *outcount; ++k) {
            finish_idx(s, indices[k], &st[k], t);
        }
    }
    if (st != statuses) {
        free(st);
    }
    free(s);
    return rc;
}

int MPI_Request_free(MPI_Request *request)
{
    reqent_t e;
    if (edumpi_enabled && request && req_take(*request, &e)) {
        e.rec->request_token = 0;
        e.rec->flags |= EDUMPI_F_NOT_COMPLETED;   /* freed without observing completion */
        edumpi_complete(e.rec);
    }
    return PMPI_Request_free(request);
}
