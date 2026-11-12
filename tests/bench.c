/* Overhead microbenchmark: time per operation (microseconds), median of REPS repetitions.
   usage: bench [iterations] */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPS 7

static int cmp(const void *a, const void *b) { double x = *(const double *) a, y = *(const double *) b; return (x > y) - (x < y); }

static double median(double *v) { qsort(v, REPS, sizeof(double), cmp); return v[REPS / 2]; }

int main(int argc, char **argv)
{
    int rank, n, iters = argc > 1 ? atoi(argv[1]) : 20000, i, r;
    char buf[1 << 16];
    double t[REPS], pp[REPS], ar[REPS], nb[REPS], bc[REPS], d[4] = {1, 2, 3, 4}, o[4];
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n);
    memset(buf, 1, sizeof(buf));
    int peer = rank ^ 1;
    for (r = 0; r < REPS; ++r) {
        MPI_Barrier(MPI_COMM_WORLD);
        double a = MPI_Wtime();
        for (i = 0; i < iters; ++i) {
            if (rank % 2 == 0) {
                MPI_Send(buf, 8, MPI_CHAR, peer, 1, MPI_COMM_WORLD);
                MPI_Recv(buf, 8, MPI_CHAR, peer, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            } else {
                MPI_Recv(buf, 8, MPI_CHAR, peer, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send(buf, 8, MPI_CHAR, peer, 1, MPI_COMM_WORLD);
            }
        }
        pp[r] = (MPI_Wtime() - a) / iters * 1e6 / 2;       /* one-way latency */
        MPI_Barrier(MPI_COMM_WORLD);
        a = MPI_Wtime();
        for (i = 0; i < iters; ++i) {
            MPI_Allreduce(d, o, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        }
        ar[r] = (MPI_Wtime() - a) / iters * 1e6;
        MPI_Barrier(MPI_COMM_WORLD);
        a = MPI_Wtime();
        for (i = 0; i < iters; ++i) {
            MPI_Request rq[2];
            MPI_Irecv(buf + 100, 8, MPI_CHAR, peer, 2, MPI_COMM_WORLD, &rq[0]);
            MPI_Isend(buf, 8, MPI_CHAR, peer, 2, MPI_COMM_WORLD, &rq[1]);
            MPI_Waitall(2, rq, MPI_STATUSES_IGNORE);
        }
        nb[r] = (MPI_Wtime() - a) / iters * 1e6;
        MPI_Barrier(MPI_COMM_WORLD);
        a = MPI_Wtime();
        for (i = 0; i < iters / 10; ++i) {
            MPI_Bcast(buf, 1 << 16, MPI_CHAR, 0, MPI_COMM_WORLD);
        }
        bc[r] = (MPI_Wtime() - a) / (iters / 10) * 1e6;
    }
    (void) t;
    if (0 == rank) {
        printf("RESULT ranks=%d iters=%d  pingpong_oneway_us=%.3f  allreduce32B_us=%.3f  irecv_isend_waitall_us=%.3f  bcast64K_us=%.3f\n",
               n, iters, median(pp), median(ar), median(nb), median(bc));
    }
    MPI_Finalize();
    return 0;
}
