/* Sink that forwards everything to two sinks (e.g. database and file). */
#include <stdlib.h>

#include "store.h"

typedef struct {
    edumpi_sink_t *a, *b;
} tee_t;

static int tee_open(edumpi_sink_t *s, int rank, int world_size, const char *node)
{
    tee_t *t = s->priv;
    int ra = t->a->open(t->a, rank, world_size, node);
    int rb = t->b->open(t->b, rank, world_size, node);
    return ra && rb ? -1 : 0;
}

static int tee_write(edumpi_sink_t *s, const edumpi_rec_t *recs, size_t n)
{
    tee_t *t = s->priv;
    if (t->a) t->a->write(t->a, recs, n);
    if (t->b) t->b->write(t->b, recs, n);
    return 0;
}

static int tee_close(edumpi_sink_t *s, const char *const *f, size_t nf, const char *const *c, size_t nc)
{
    tee_t *t = s->priv;
    if (t->a) t->a->close(t->a, f, nf, c, nc);
    if (t->b) t->b->close(t->b, f, nf, c, nc);
    free(t);
    free(s);
    return 0;
}

edumpi_sink_t *edumpi_tee_sink_new(edumpi_sink_t *a, edumpi_sink_t *b)
{
    edumpi_sink_t *s = calloc(1, sizeof(*s));
    tee_t *t = calloc(1, sizeof(*t));
    if (!a || !b || !s || !t) {
        free(s);
        free(t);
        return a ? a : b;
    }
    t->a = a;
    t->b = b;
    s->priv = t;
    s->open = tee_open;
    s->write = tee_write;
    s->close = tee_close;
    return s;
}
