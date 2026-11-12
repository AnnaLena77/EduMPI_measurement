/* Plain MPI program with known traffic; tests/check_calls.py verifies what the tool recorded.
   Run with 4 ranks. */
#include <mpi.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int rank, n, i;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n);
    int next = (rank + 1) % n, prev = (rank + n - 1) % n;
    char buf[1000], rbuf[1000];
    memset(buf, rank, sizeof(buf));

    /* 1: blocking ring, 1000 bytes (even ranks send first) */
    if (rank % 2 == 0) {
        MPI_Send(buf, 1000, MPI_CHAR, next, 5, MPI_COMM_WORLD);
        MPI_Recv(rbuf, 1000, MPI_CHAR, prev, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        MPI_Recv(rbuf, 1000, MPI_CHAR, prev, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Send(buf, 1000, MPI_CHAR, next, 5, MPI_COMM_WORLD);
    }

    /* 2: non-blocking ring, 200 ints, receive from any source */
    int sb[200], rb[200];
    MPI_Request rq[2];
    for (i = 0; i < 200; ++i) sb[i] = rank;
    MPI_Irecv(rb, 200, MPI_INT, MPI_ANY_SOURCE, 6, MPI_COMM_WORLD, &rq[0]);
    MPI_Isend(sb, 200, MPI_INT, next, 6, MPI_COMM_WORLD, &rq[1]);
    MPI_Waitall(2, rq, MPI_STATUSES_IGNORE);

    /* 3: Sendrecv ring, 64 bytes */
    MPI_Sendrecv(buf, 64, MPI_CHAR, next, 7, rbuf, 64, MPI_CHAR, prev, 7, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* 4: Bcast 100 ints from root 1 */
    int ib[100] = {0};
    MPI_Bcast(ib, 100, MPI_INT, 1, MPI_COMM_WORLD);

    /* 5: Allreduce 10 doubles */
    double d[10] = {0}, dr[10];
    MPI_Allreduce(d, dr, 10, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    /* 6: Gather 4 ints to root 0 */
    int g[4] = {rank, rank, rank, rank};
    int *gall = rank == 0 ? malloc(sizeof(int) * 4 * n) : NULL;
    MPI_Gather(g, 4, MPI_INT, gall, 4, MPI_INT, 0, MPI_COMM_WORLD);
    free(gall);

    /* 7: Alltoallv with 1 int per peer */
    int *sc = malloc(sizeof(int) * n), *sd = malloc(sizeof(int) * n), *s1 = malloc(sizeof(int) * n), *r1 = malloc(sizeof(int) * n);
    for (i = 0; i < n; ++i) { sc[i] = 1; sd[i] = i; s1[i] = i; }
    MPI_Alltoallv(s1, sc, sd, MPI_INT, r1, sc, sd, MPI_INT, MPI_COMM_WORLD);
    free(sc); free(sd); free(s1); free(r1);

    /* 8: split and dup communicators */
    MPI_Comm half, dup;
    MPI_Comm_split(MPI_COMM_WORLD, rank % 2, rank, &half);
    MPI_Allreduce(d, dr, 10, MPI_DOUBLE, MPI_SUM, half);
    MPI_Comm_dup(MPI_COMM_WORLD, &dup);
    MPI_Barrier(dup);
    MPI_Comm_free(&half);
    MPI_Comm_free(&dup);

    /* 9: non-blocking collective */
    MPI_Request ir;
    int ib2[50] = {0};
    MPI_Ibcast(ib2, 50, MPI_INT, 0, MPI_COMM_WORLD, &ir);
    MPI_Wait(&ir, MPI_STATUS_IGNORE);

    /* 10: one-sided */
    int wbuf[100] = {0}, src[10] = {1};
    MPI_Win win;
    MPI_Win_create(wbuf, sizeof(wbuf), sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win);
    MPI_Win_fence(0, win);
    MPI_Put(src, 10, MPI_INT, next, 0, 10, MPI_INT, win);
    MPI_Win_fence(0, win);
    MPI_Win_free(&win);

    MPI_Finalize();
    return 0;
}
