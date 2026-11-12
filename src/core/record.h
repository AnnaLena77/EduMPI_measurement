/*
 * One record per MPI call (successor of Open MPI fork's qentry).
 *
 * The first 64 bytes hold everything a point-to-point call touches, so a call dirties one
 * cache line; the second line holds collective details and the late sender/receiver times.
 * Records live in segments (store.h) and are written into by pointer, also by a later
 * request-completion callback, so nothing has to be looked up by request.
 */
#ifndef EDUMPI_RECORD_H
#define EDUMPI_RECORD_H

#include <stdatomic.h>
#include <stdint.h>

#define EDUMPI_NO_FUNC 0xFFFFu
#define EDUMPI_NO_COMM 0xFFFFu

/* flags */
enum {
    EDUMPI_F_BLOCKING   = 1u << 0,
    EDUMPI_F_COLLECTIVE = 1u << 1,  /* communicationtype collective (else p2p) */
    EDUMPI_F_ONESIDED   = 1u << 2,  /* communicationtype one-sided */
    EDUMPI_F_HAS_DONE   = 1u << 3,  /* done_ns is valid */
    EDUMPI_F_ANY_SOURCE = 1u << 4,  /* receive posted with MPI_ANY_SOURCE; partner is the real sender */
    EDUMPI_F_NOT_COMPLETED = 1u << 5, /* request had not completed when the record was written */
    EDUMPI_F_NO_END     = 1u << 6,  /* call had not returned when the record was written */
    EDUMPI_F_MPIT_SUM   = 1u << 7,  /* collective: send/recv bytes and partner bitmap are sums over the library's internal messages (MPI_T) */
    EDUMPI_F_EVENT_DONE = 1u << 8,  /* done_ns, partner and recv_bytes come from MPI_T completion events, not from a wait or test */
};

/* state of a record in the store */
enum { EDUMPI_S_FREE = 0, EDUMPI_S_OPEN = 1, EDUMPI_S_COMPLETE = 2 };

typedef struct edumpi_rec {
    /* line 1: hot */
    uint64_t start_ns;      /* CLOCK_REALTIME ns; MPI_Irecv: time posted */
    uint64_t end_ns;        /* the call returned */
    uint64_t done_ns;       /* request completed (arrival of a message / send finished), if HAS_DONE */
    int64_t send_bytes;
    int64_t recv_bytes;
    int32_t partner;        /* world rank, -1 unknown */
    uint16_t comm;          /* index into the communicator table */
    uint16_t func;          /* index into the function table */
    uint32_t flags;
    uint32_t seq;           /* number of the call on this rank */
    _Atomic uint32_t state; /* EDUMPI_S_* ; written last, with release */
    /* line 2: collectives and matching */
    uint32_t sendcount, recvcount;
    uint32_t coll_alg[3];   /* algorithm ids (component << 16 | id): user comm, inter-node, intra-node; 0: none */
    uint64_t late_sender_ns;   /* receiver waited for the sender; 0: none */
    uint64_t late_receiver_ns; /* sender waited for the receiver; 0: none */
    uint64_t *partner_bits;    /* bitmap of world ranks sent to inside a collective (malloc, freed by the store) */
    uint32_t partner_bits_words;
    uint32_t pad2;
    uint64_t request_token;    /* MPI_T request token while the request is open, else 0 */
    uint64_t pad3;
} edumpi_rec_t;

_Static_assert(sizeof(edumpi_rec_t) == 128, "record must be 128 bytes");

#endif
