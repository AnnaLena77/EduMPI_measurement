/* File sink: one binary file per rank, format in docs/LOG_FORMAT.md. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"

typedef struct {
    FILE *f;
    int rank, world_size;
    char node[32];
    char dir[512];
} file_priv_t;

static void put32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(unsigned char *p, uint64_t v) { memcpy(p, &v, 8); }

static void header(file_priv_t *fp, uint64_t trailer_offset)
{
    unsigned char h[64] = {0};
    memcpy(h, "EDUMPIT1", 8);
    put32(h + 8, 1);                         /* version */
    put32(h + 12, sizeof(edumpi_rec_t));     /* record size */
    put32(h + 16, (uint32_t) fp->rank);
    put32(h + 20, (uint32_t) fp->world_size);
    memcpy(h + 24, fp->node, 31);
    put64(h + 56, trailer_offset);
    fseek(fp->f, 0, SEEK_SET);
    fwrite(h, 1, 64, fp->f);
    fseek(fp->f, 0, SEEK_END);
}

static int file_open(edumpi_sink_t *s, int rank, int world_size, const char *node)
{
    file_priv_t *fp = s->priv;
    char path[1024];

    snprintf(path, sizeof(path), "%s/edumpi.%d.bin", fp->dir, rank);
    fp->f = fopen(path, "w+b");
    if (NULL == fp->f) {
        return -1;
    }
    fp->rank = rank;
    fp->world_size = world_size;
    strncpy(fp->node, node ? node : "", sizeof(fp->node) - 1);
    header(fp, 0);
    return 0;
}

static int file_write(edumpi_sink_t *s, const edumpi_rec_t *recs, size_t n)
{
    file_priv_t *fp = s->priv;
    for (size_t i = 0; i < n; ++i) {
        edumpi_rec_t copy = recs[i];
        copy.partner_bits = NULL;     /* a bitmap follows the record: partner_bits_words * 8 bytes */
        fwrite(&copy, sizeof(copy), 1, fp->f);
        if (recs[i].partner_bits && recs[i].partner_bits_words) {
            fwrite(recs[i].partner_bits, 8, recs[i].partner_bits_words, fp->f);
        }
    }
    return 0;
}

static int file_close(edumpi_sink_t *s, const char *const *funcs, size_t nf, const char *const *comms, size_t nc)
{
    file_priv_t *fp = s->priv;
    unsigned char t[16] = {0};
    char name[64];
    long off = ftell(fp->f);

    memcpy(t, "EDUMPITR", 8);
    put32(t + 8, (uint32_t) nf);
    put32(t + 12, (uint32_t) nc);
    fwrite(t, 1, 16, fp->f);
    for (size_t i = 0; i < nf; ++i) {
        memset(name, 0, 32);
        strncpy(name, funcs[i], 31);
        fwrite(name, 1, 32, fp->f);
    }
    for (size_t i = 0; i < nc; ++i) {
        memset(name, 0, 64);
        strncpy(name, comms[i], 63);
        fwrite(name, 1, 64, fp->f);
    }
    header(fp, (uint64_t) off);
    fclose(fp->f);
    free(fp);
    free(s);
    return 0;
}

edumpi_sink_t *edumpi_file_sink_new(const char *dir)
{
    edumpi_sink_t *s = calloc(1, sizeof(*s));
    file_priv_t *fp = calloc(1, sizeof(*fp));
    if (!s || !fp) {
        free(s);
        free(fp);
        return NULL;
    }
    snprintf(fp->dir, sizeof(fp->dir), "%s", dir ? dir : ".");
    s->priv = fp;
    s->open = file_open;
    s->write = file_write;
    s->close = file_close;
    return s;
}
