/* coll/adapt (run with --mca coll_adapt_priority 100): Ibcast and Ireduce with other traffic between start and wait
   (4 ranks, count 10 doubles for A, 100 for B). rep 0: A alone; rep 1: A, B, a blocking Bcast / Reduce of A's size and a
   Sendrecv ring, then Waitall. The records of A in rep 0 and rep 1 must be identical (bytes, partners). */
#include <mpi.h>

int main(int argc, char **argv)
{
    int rank, n;
    double a[10], b[100], c[10], r[100];
    char sbuf[64] = {0}, rbuf[64];
    MPI_Request rq[2];
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n);
    for (int i = 0; i < 100; ++i) b[i] = i;
    for (int i = 0; i < 10; ++i) a[i] = c[i] = rank;
    MPI_Barrier(MPI_COMM_WORLD);
    for (int op = 0; op < 2; ++op) {            /* 0: Ibcast, 1: Ireduce */
        for (int rep = 0; rep < 2; ++rep) {
            if (0 == op) {
                MPI_Ibcast(a, 10, MPI_DOUBLE, 0, MPI_COMM_WORLD, &rq[0]);
            } else {
                MPI_Ireduce(a, r, 10, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD, &rq[0]);
            }
            if (0 == rep) {
                MPI_Wait(&rq[0], MPI_STATUS_IGNORE);
            } else {
                if (0 == op) {
                    MPI_Ibcast(b, 100, MPI_DOUBLE, 0, MPI_COMM_WORLD, &rq[1]);
                    MPI_Bcast(c, 10, MPI_DOUBLE, 0, MPI_COMM_WORLD);
                } else {
                    MPI_Ireduce(b, r, 100, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD, &rq[1]);
                    MPI_Reduce(c, r, 10, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
                }
                MPI_Sendrecv(sbuf, 64, MPI_CHAR, (rank + 1) % n, 3, rbuf, 64, MPI_CHAR, (rank + n - 1) % n, 3,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Waitall(2, rq, MPI_STATUSES_IGNORE);
            }
            MPI_Barrier(MPI_COMM_WORLD);
        }
    }
    MPI_Finalize();
    return 0;
}
