/* Helpers used by the generated wrappers (wrappers.c) and by the request tracking (track.c). */
#ifndef EDUMPI_WRAP_H
#define EDUMPI_WRAP_H

#include <mpi.h>
#include <stdint.h>

#include "../core/store.h"

extern int edumpi_enabled;   /* set by MPI_Init wrapper, cleared in MPI_Finalize */

int64_t edumpi_tsize(MPI_Datatype dt);                 /* bytes of one element, 0 if unknown */
#define EB(count, dt) ((int64_t) (count) * edumpi_tsize(dt))
int edumpi_crank(MPI_Comm comm);                       /* cached rank / size */
int edumpi_csize(MPI_Comm comm);
uint16_t edumpi_cid(MPI_Comm comm);                    /* index into the communicator table */
int edumpi_world(MPI_Comm comm, int rank);             /* rank in MPI_COMM_WORLD, -1 unknown */
uint16_t edumpi_wid(MPI_Win win);
int edumpi_wworld(MPI_Win win, int rank);
int64_t edumpi_vsum(const int *counts, int n, MPI_Datatype dt);
int64_t edumpi_wsum(const int *counts, const MPI_Datatype *types, int n);

/* MPI_T backend (src/mpit/mpit.c) */
extern int edumpi_mpit_active;                   /* 1 if the library offers the events and they are registered */
extern _Thread_local edumpi_rec_t *edumpi_tls_cur;   /* record of the MPI call this thread is in */
extern _Thread_local int edumpi_tls_op;          /* COLLTYPE of that call, -1 if not a collective */
void edumpi_mpit_init(void);                     /* before PMPI_Init */
void edumpi_mpit_after_init(void);               /* after PMPI_Init: register on MPI_COMM_WORLD / SELF */
void edumpi_mpit_finalize(void);                 /* before PMPI_Finalize */
/* completion data from an event; token = MPI_Request handle. Returns 1 if applied, 0 if stashed for later. */
int edumpi_req_fill(uintptr_t token, uint64_t when_ns, int partner, int64_t bytes, int is_recv);

/* request tracking: a non-blocking call leaves its record open until the request completes */
enum { EDUMPI_REQ_SEND = 0, EDUMPI_REQ_RECV = 1, EDUMPI_REQ_COLL = 2, EDUMPI_REQ_WIN = 3 };
void edumpi_req_add(MPI_Request req, edumpi_rec_t *rec, MPI_Comm comm, int kind);
void edumpi_finish_after_call(edumpi_rec_t *r, MPI_Comm comm, int win);   /* sets end_ns, completes */

static inline edumpi_rec_t *edumpi_rec_begin(const char *name, uint16_t *cache_f, uint16_t comm_id, uint32_t flags)
{
    if (!edumpi_enabled) {
        return NULL;
    }
    if (0xFFFF == *cache_f) {
        *cache_f = edumpi_func_id(name);
    }
    return edumpi_begin(*cache_f, comm_id, flags);
}

#endif
