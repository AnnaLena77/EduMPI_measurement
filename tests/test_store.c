/*
 * Store test without MPI: several threads create records, some stay open and are completed later
 * by other threads, the monitor thread flushes concurrently; at the end every sequence number
 * must be in the file exactly once, with the data its creator wrote.
 * usage: test_store <dir> [threads] [records per thread]
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/store.h"

static int nthreads = 8;
static long per_thread = 200000;
static edumpi_rec_t *volatile open_slots[4096];

static void *worker(void *arg)
{
    long id = (long) arg;
    uint16_t f = edumpi_func_id(id % 2 ? "MPI_Isend" : "MPI_Irecv");
    uint16_t c = edumpi_comm_id(id % 2 ? "MPI_COMM_WORLD" : "MPI COMM 3 DUP FROM 0");
    for (long i = 0; i < per_thread; ++i) {
        edumpi_rec_t *r = edumpi_begin(f, c, 0);
        if (!r) {
            continue;
        }
        r->send_bytes = (int64_t) id * 1000000 + i;   /* creator and index, checked after the run */
        r->end_ns = edumpi_now_ns();
        if (0 == i % 1000) {                          /* stays open, completed later by another thread */
            edumpi_rec_t *old = __sync_lock_test_and_set(&open_slots[(id * 64 + i / 1000) % 4096], r);
            if (old) {
                edumpi_complete(old);
            }
        } else {
            edumpi_complete(r);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    pthread_t th[64];
    char path[512];
    unsigned char *seen;
    uint64_t total;
    FILE *f;
    unsigned char h[64];
    edumpi_rec_t rec;
    uint64_t n = 0, bad = 0, dup = 0;

    if (argc > 2) nthreads = atoi(argv[2]);
    if (argc > 3) per_thread = atol(argv[3]);
    total = (uint64_t) nthreads * per_thread;

    edumpi_store_init(3, 8, "testnode", edumpi_file_sink_new(dir), 20);
    for (long i = 0; i < nthreads; ++i) pthread_create(&th[i], NULL, worker, (void *) i);
    for (long i = 0; i < nthreads; ++i) pthread_join(th[i], NULL);
    for (int i = 0; i < 4096; ++i) edumpi_complete(open_slots[i]);   /* the rest */
    printf("claimed %llu dropped %llu written(before finalize) %llu\n", (unsigned long long) edumpi_store_claimed(),
           (unsigned long long) edumpi_store_dropped(), (unsigned long long) edumpi_store_written());
    edumpi_store_finalize();

    snprintf(path, sizeof(path), "%s/edumpi.3.bin", dir);
    f = fopen(path, "rb");
    if (!f || 64 != fread(h, 1, 64, f) || memcmp(h, "EDUMPIT1", 8)) { printf("FAIL: header\n"); return 1; }
    seen = calloc(total, 1);
    while (1 == fread(&rec, sizeof(rec), 1, f)) {
        long id;
        if (n >= total) break;
        id = (long) (rec.send_bytes / 1000000);
        if (rec.seq >= total) { ++bad; break; }
        if (seen[rec.seq]) ++dup;
        seen[rec.seq] = 1;
        if (id < 0 || id >= nthreads || rec.state != EDUMPI_S_COMPLETE || 0 == rec.start_ns || rec.end_ns < rec.start_ns) ++bad;
        ++n;
    }
    fclose(f);
    printf("records %llu of %llu, duplicates %llu, bad %llu\n", (unsigned long long) n, (unsigned long long) total,
           (unsigned long long) dup, (unsigned long long) bad);
    if (n != total || dup || bad || edumpi_store_dropped()) { printf("FAIL\n"); return 1; }
    printf("PASS\n");
    return 0;
}
