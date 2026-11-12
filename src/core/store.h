/*
 * Event store: a table of non-moving segments of EDUMPI_SEG_EVENTS records.
 *
 * edumpi_begin() claims a record with one atomic add (no lock), fills the hot fields and returns
 * a pointer. The call (or a later request completion, possibly on another thread) finishes the
 * record with edumpi_complete(). A segment is written out and recycled by the monitor thread
 * once every record in it is complete; an open record only keeps its own segment in memory.
 */
#ifndef EDUMPI_STORE_H
#define EDUMPI_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "record.h"

#define EDUMPI_SEG_EVENTS 2048
#define EDUMPI_MAX_SEGS 65536    /* 134 million records per rank */

typedef struct edumpi_sink {
    int (*open)(struct edumpi_sink *, int rank, int world_size, const char *node);
    int (*write)(struct edumpi_sink *, const edumpi_rec_t *recs, size_t n);
    int (*close)(struct edumpi_sink *, const char *const *funcs, size_t nf, const char *const *comms, size_t nc);
    void *priv;
} edumpi_sink_t;

/* sink may be NULL (records are dropped when flushed: tests of the store only). */
int edumpi_store_init(int rank, int world_size, const char *node, edumpi_sink_t *sink, unsigned flush_ms);
/* writes everything, stops the monitor thread, closes the sink */
void edumpi_store_finalize(void);

/* names of calls and communicators (interned, returns the table index) */
uint16_t edumpi_func_id(const char *name);
uint16_t edumpi_comm_id(const char *label);

/* returns NULL if the store is not initialised or full (counted in edumpi_store_dropped()) */
edumpi_rec_t *edumpi_begin(uint16_t func, uint16_t comm, uint32_t flags);
void edumpi_complete(edumpi_rec_t *r);

/* write out complete segments now (monitor thread does this periodically); force: everything */
void edumpi_store_flush(int force);

uint64_t edumpi_store_claimed(void);
uint64_t edumpi_store_dropped(void);
uint64_t edumpi_store_written(void);

uint64_t edumpi_now_ns(void);

/* file sink: $dir/edumpi.<rank>.bin, format in docs/LOG_FORMAT.md */
edumpi_sink_t *edumpi_file_sink_new(const char *dir);

#endif
