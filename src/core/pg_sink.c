/*
 * PostgreSQL sink: streams the records with binary COPY into the table edumpi_running_data of
 * EduStore (schema: EduStore/edustore_schema.sql of the EduMPI repository), with the columns and the
 * meaning the EduMPI GUI expects. Built with -DEDUMPI_WITH_PG and libpq.
 *
 * Environment (as in EduMPI): DB_HOST, DB_PORT, DB_USER, DB_NAME, DB_PW; run id SLURM_JOBID (or
 * EDUMPI_RUN_ID); EDUMPI_PROGRAM (program name), rank 0 inserts the run, every rank its cluster row.
 *
 * Row semantics (unchanged from the version with the modified Open MPI):
 *   time_start  call start, rounded down to the second   time_end  call end (MPI_Irecv: arrival)
 *   time_diff   (time_end - exact start) in seconds       latesendertime / laterecvrtime in seconds
 *   coll_partnerranks  bitmap of 50 bytes (bit r%8 of byte r/8 = world rank r), empty for p2p
 *   a non-blocking send that was still in flight gets a second row "MPI_Isend_done"
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "store.h"

#ifdef EDUMPI_WITH_PG
#include <libpq-fe.h>

typedef struct {
    PGconn *conn;
    char *buf;
    size_t len, cap;
    int run_id;
    int world_size;
    int copy_open;
} pg_priv_t;

static const char COPY_QUERY[] = "COPY edumpi_running_data FROM STDIN (FORMAT binary)";
static const unsigned char COPY_HEADER[19] = {'P', 'G', 'C', 'O', 'P', 'Y', '\n', 0xff, '\r', '\n', 0, 0, 0, 0, 0, 0, 0, 0, 0};

static void grow(pg_priv_t *p, size_t extra)
{
    if (p->len + extra > p->cap) {
        p->cap = (p->len + extra) * 2;
        p->buf = realloc(p->buf, p->cap);
    }
}

static void put_be(pg_priv_t *p, uint64_t v, int bytes)
{
    grow(p, bytes);
    for (int i = bytes - 1; i >= 0; --i) {
        p->buf[p->len++] = (char) ((v >> (8 * i)) & 0xff);
    }
}

static void f_int4(pg_priv_t *p, int32_t v) { put_be(p, 4, 4); put_be(p, (uint32_t) v, 4); }
static void f_int8(pg_priv_t *p, int64_t v) { put_be(p, 8, 4); put_be(p, (uint64_t) v, 8); }
static void f_double(pg_priv_t *p, double d) { uint64_t u; memcpy(&u, &d, 8); put_be(p, 8, 4); put_be(p, u, 8); }
static void f_text(pg_priv_t *p, const char *s)
{
    size_t n = strlen(s);
    put_be(p, n, 4);
    grow(p, n);
    memcpy(p->buf + p->len, s, n);
    p->len += n;
}
/* timestamptz: microseconds since 2000-01-01 UTC */
static void f_ts(pg_priv_t *p, uint64_t ns, int round_seconds)
{
    int64_t us = (int64_t) (ns / 1000) - 946684800LL * 1000000LL;
    if (round_seconds) {
        us -= us % 1000000LL;
    }
    put_be(p, 8, 4);
    put_be(p, (uint64_t) us, 8);
}
static void f_bytea(pg_priv_t *p, const unsigned char *d, size_t n)
{
    put_be(p, n, 4);
    grow(p, n);
    if (n) memcpy(p->buf + p->len, d, n);
    p->len += n;
}

static void row(pg_priv_t *p, const char *func, const char *type, int64_t sb, int64_t rb, const char *area,
                int partner, const char *alg, uint64_t start_ns, uint64_t end_ns, double ls, double lr,
                const unsigned char *bits, size_t nbits)
{
    double diff = end_ns > start_ns ? (double) (end_ns - start_ns) * 1e-9 : 0.0;
    put_be(p, 16, 2);
    f_int4(p, p->run_id);
    f_text(p, func);
    f_text(p, type);
    f_int8(p, sb);
    f_int8(p, rb);
    f_text(p, area);
    f_text(p, edumpi_g_node);
    f_int4(p, edumpi_g_rank);
    f_int4(p, partner);
    f_text(p, alg);
    f_ts(p, start_ns, 1);
    f_ts(p, end_ns, 0);
    f_double(p, ls);
    f_double(p, lr);
    f_double(p, diff);
    f_bytea(p, bits, nbits);
}

static int is_isend(const char *f)
{
    return !strcmp(f, "MPI_Isend") || !strcmp(f, "MPI_Issend") || !strcmp(f, "MPI_Ibsend") || !strcmp(f, "MPI_Irsend");
}

static void flush_copy(pg_priv_t *p)
{
    if (p->len > 0 && p->copy_open) {
        if (1 != PQputCopyData(p->conn, p->buf, (int) p->len)) {
            fprintf(stderr, "edumpi: COPY failed: %s\n", PQerrorMessage(p->conn));
        }
    }
    p->len = 0;
}

static int pg_open(edumpi_sink_t *s, int rank, int world_size, const char *node)
{
    pg_priv_t *p = s->priv;
    const char *host = getenv("DB_HOST"), *port = getenv("DB_PORT"), *user = getenv("DB_USER"),
               *name = getenv("DB_NAME"), *pw = getenv("DB_PW");
    const char *job = getenv("EDUMPI_RUN_ID");
    char conninfo[1024], rankstr[16], runstr[32];
    PGresult *res;

    if (!job) job = getenv("SLURM_JOBID");
    if (!host || !port || !user || !name || !pw || !job) {
        fprintf(stderr, "edumpi: DB_HOST, DB_PORT, DB_USER, DB_NAME, DB_PW and SLURM_JOBID (or EDUMPI_RUN_ID) must be set\n");
        return -1;
    }
    p->run_id = atoi(job);
    p->world_size = world_size;
    snprintf(runstr, sizeof(runstr), "%d", p->run_id);
    snprintf(rankstr, sizeof(rankstr), "%d", rank);
    snprintf(conninfo, sizeof(conninfo), "host=%s port=%s dbname=%s user=%s password=%s", host, port, name, user, pw);
    p->conn = PQconnectdb(conninfo);
    if (PQstatus(p->conn) != CONNECTION_OK) {
        fprintf(stderr, "edumpi: database connection failed: %s", PQerrorMessage(p->conn));
        return -1;
    }
    if (0 == rank) {
        const char *home = getenv("HOME");
        const char *user_id = home && strrchr(home, '/') ? strrchr(home, '/') + 1 : "unknown";
        const char *prog = getenv("EDUMPI_PROGRAM");
        char start[40];
        time_t now = time(NULL);
        const char *vals[4];
        strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", localtime(&now));
        vals[0] = runstr; vals[1] = start; vals[2] = user_id; vals[3] = prog ? prog : "";
        res = PQexecParams(p->conn, "INSERT INTO edumpi_runs (edumpi_run_id, start_time, user_id, program_name) "
                           "VALUES ($1, $2, $3, $4) ON CONFLICT DO NOTHING", 4, NULL, vals, NULL, NULL, 0);
        PQclear(res);
    } else {
        for (int i = 0; i < 300; ++i) {          /* wait for rank 0 to insert the run (30 s) */
            const char *vals[1] = {runstr};
            int found;
            res = PQexecParams(p->conn, "SELECT 1 FROM edumpi_runs WHERE edumpi_run_id = $1", 1, NULL, vals, NULL, NULL, 0);
            found = PQntuples(res) > 0;
            PQclear(res);
            if (found) break;
            usleep(100000);
        }
    }
    {
        const char *vals[3] = {runstr, node, rankstr};
        res = PQexecParams(p->conn, "INSERT INTO edumpi_cluster_info (edumpi_run_id, processorname, processrank) VALUES ($1, $2, $3)",
                           3, NULL, vals, NULL, NULL, 0);
        PQclear(res);
    }
    res = PQexec(p->conn, COPY_QUERY);
    if (PQresultStatus(res) != PGRES_COPY_IN) {
        fprintf(stderr, "edumpi: COPY start failed: %s", PQerrorMessage(p->conn));
        PQclear(res);
        return -1;
    }
    PQclear(res);
    p->copy_open = 1;
    p->cap = 1 << 16;
    p->buf = malloc(p->cap);
    memcpy(p->buf, COPY_HEADER, 19);
    p->len = 19;
    return 0;
}

static int pg_write(edumpi_sink_t *s, const edumpi_rec_t *recs, size_t n)
{
    pg_priv_t *p = s->priv;
    if (!p->copy_open) {
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        const edumpi_rec_t *r = &recs[i];
        const char *func = edumpi_func_name(r->func);
        const char *area = r->comm == EDUMPI_NO_COMM ? "" : edumpi_comm_name(r->comm);
        const char *type = (r->flags & EDUMPI_F_COLLECTIVE) ? "collective" : (r->flags & EDUMPI_F_ONESIDED) ? "one-sided" : "p2p";
        char alg[160] = "";
        unsigned char bits[400];
        size_t nbits = 0;
        uint64_t end = r->end_ns;
        int recv_arrival = !strcmp(func, "MPI_Irecv") && (r->flags & EDUMPI_F_HAS_DONE) && r->done_ns > r->start_ns;

        if (recv_arrival) {
            end = r->done_ns;                       /* the row of a receive ends with the arrival of the message */
        } else if (0 == end) {
            end = r->start_ns;
        }
        if ((r->flags & EDUMPI_F_COLLECTIVE) && edumpi_algorithm_text) {
            edumpi_algorithm_text(r, alg, sizeof(alg));
        }
        if ((r->flags & EDUMPI_F_COLLECTIVE) && r->partner_bits && r->partner_bits_words) {
            size_t world_bytes = (size_t) (p->world_size + 7) / 8, want = world_bytes < 50 ? 50 : world_bytes;
            int any = 0;
            nbits = want > sizeof(bits) ? sizeof(bits) : want;
            memset(bits, 0, nbits);
            for (size_t b = 0; b < nbits && b / 8 < r->partner_bits_words; ++b) {
                bits[b] = (unsigned char) ((r->partner_bits[b / 8] >> (8 * (b % 8))) & 0xff);
                any |= bits[b];
            }
            if (!any) nbits = 0;
        }
        row(p, func, type, r->send_bytes, r->recv_bytes, area, r->partner, alg, r->start_ns, end,
            (double) r->late_sender_ns * 1e-9, (double) r->late_receiver_ns * 1e-9, bits, nbits);
        if (is_isend(func) && (r->flags & EDUMPI_F_HAS_DONE) && r->done_ns > r->end_ns) {
            row(p, "MPI_Isend_done", "p2p", 0, 0, area, r->partner, "", r->start_ns, r->done_ns, 0.0, 0.0, NULL, 0);
        }
        if (p->len > (1 << 15)) {
            flush_copy(p);
        }
    }
    flush_copy(p);
    return 0;
}

static int pg_close(edumpi_sink_t *s, const char *const *f, size_t nf, const char *const *c, size_t nc)
{
    pg_priv_t *p = s->priv;
    PGresult *res;
    (void) f; (void) nf; (void) c; (void) nc;
    if (p->copy_open) {
        char trailer[2] = {(char) 0xff, (char) 0xff};
        PQputCopyData(p->conn, trailer, 2);
        PQputCopyEnd(p->conn, NULL);
        res = PQgetResult(p->conn);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "edumpi: COPY into edumpi_running_data failed: %s", PQerrorMessage(p->conn));
        }
        PQclear(res);
    }
    if (0 == edumpi_g_rank && p->conn) {
        char runstr[32];
        const char *vals[1] = {runstr};
        snprintf(runstr, sizeof(runstr), "%d", p->run_id);
        res = PQexecParams(p->conn, "UPDATE edumpi_runs SET end_time = now() WHERE edumpi_run_id = $1", 1, NULL, vals, NULL, NULL, 0);
        PQclear(res);
    }
    if (p->conn) PQfinish(p->conn);
    free(p->buf);
    free(p);
    free(s);
    return 0;
}

edumpi_sink_t *edumpi_pg_sink_new(void)
{
    edumpi_sink_t *s = calloc(1, sizeof(*s));
    pg_priv_t *p = calloc(1, sizeof(*p));
    if (!s || !p) {
        free(s);
        free(p);
        return NULL;
    }
    s->priv = p;
    s->open = pg_open;
    s->write = pg_write;
    s->close = pg_close;
    return s;
}
#else
edumpi_sink_t *edumpi_pg_sink_new(void)
{
    fprintf(stderr, "edumpi: built without PostgreSQL support (make PG=1)\n");
    return NULL;
}
#endif
