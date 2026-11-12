/*
 * Name table of the Open MPI backend: semantic names of the entity sets (proposal for the MPI Forum Tools WG
 * "entities and sets" side document, see section 7 of the plan) -> Open MPI event names, element count and
 * whether the set member is required. Another MPI library gets its own table; the rest of the tool uses only
 * the semantic names (enum values).
 *
 *   set REQUEST   REQUEST_COMPLETED(kind)                      generic request states, kind selects the owner
 *   set P2P       SEND_START / RECV_START / SEND_COMPLETE / RECV_COMPLETE
 *                                                              message envelopes, any PML; the start events carry
 *                                                              the collective request as parent
 *   set MATCH     REQUEST_ACTIVATE / MATCHED_POSTED          (ob1 only) matching engine
 *   set TRANSFER  TRANSFER_STEP                              (ob1 only) data movement
 *   set COLL      ALGORITHM_SELECTED                         algorithm chosen, per level
 */
#ifndef EDUMPI_NAMES_OMPI_H
#define EDUMPI_NAMES_OMPI_H

enum { EV_SEND_START, EV_RECV_START, EV_SEND_COMPLETE, EV_RECV_COMPLETE, EV_ALGORITHM,
       EV_ACTIVATE, EV_MATCH, EV_TRANSFER, EV_REQ_COMPLETED, EV_N };

typedef struct {
    const char *semantic;        /* name in the entity set catalogue */
    const char *impl;            /* name in this MPI library */
    int elements;                /* expected number of payload elements */
    int required;                /* without it the backend is not used */
} evname_t;

static const evname_t ev_table[EV_N] = {
    [EV_SEND_START]     = {"P2P_SEND_START",           "ompi.pml_send_start",           7, 1},
    [EV_RECV_START]     = {"P2P_RECV_START",           "ompi.pml_recv_start",           7, 0},
    [EV_SEND_COMPLETE]  = {"P2P_SEND_COMPLETE",        "ompi.pml_send_complete",        6, 0},
    [EV_RECV_COMPLETE]  = {"P2P_RECV_COMPLETE",        "ompi.pml_recv_complete",        6, 1},
    [EV_ALGORITHM]      = {"COLL_ALGORITHM_SELECTED",  "ompi.coll_algorithm_selected",  9, 0},
    [EV_ACTIVATE]       = {"MATCH_REQUEST_ACTIVATE",   "ompi.pml_ob1_request_activate", 1, 0},
    [EV_MATCH]          = {"MATCH_POSTED",             "ompi.pml_ob1_posted_match",     1, 0},
    [EV_TRANSFER]       = {"TRANSFER_STEP",            "ompi.pml_ob1_transfer",         2, 0},
    [EV_REQ_COMPLETED]  = {"REQUEST_COMPLETED",        "ompi.mpi.request_completed",    4, 0},
};

/* kinds of the REQUEST set (element "kind") */
enum { REQ_KIND_PML_SEND = 0, REQ_KIND_PML_RECV = 1, REQ_KIND_COLL = 2, REQ_KIND_OSC = 3, REQ_KIND_IO = 4,
       REQ_KIND_GENERALIZED = 5, REQ_KIND_PART = 6 };

#endif
