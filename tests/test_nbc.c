/* Non-blocking collectives with other traffic between start and wait (4 ranks).
   rep 0: Iallreduce A (10 doubles) alone, then MPI_Wait.
   rep 1: Iallreduce A, Iallreduce B (100 doubles), a blocking Allreduce of the same size as A, a Sendrecv ring,
          then Waitall. The records of A in rep 0 and rep 1 must be identical (bytes, partners): nothing of B, of the
          blocking Allreduce or of the Sendrecv may leak into A. */
#include <mpi.h>
#include <string.h>

int main(int argc, char **argv)
{
    int rank, n;
    double a_in[10], a_out[10], b_in[100], b_out[100], c_in[10], c_out[10];
    char sbuf[64] = {0}, rbuf[64];
    MPI_Request rq[2];
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n);
    for (int i = 0; i < 100; ++i) b_in[i] = i;
    for (int i = 0; i < 10; ++i) a_in[i] = c_in[i] = rank;
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Iallreduce(a_in, a_out, 10, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD, &rq[0]);
    MPI_Wait(&rq[0], MPI_STATUS_IGNORE);
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Iallreduce(a_in, a_out, 10, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD, &rq[0]);
    MPI_Iallreduce(b_in, b_out, 100, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD, &rq[1]);
    MPI_Allreduce(c_in, c_out, 10, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Sendrecv(sbuf, 64, MPI_CHAR, (rank + 1) % n, 3, rbuf, 64, MPI_CHAR, (rank + n - 1) % n, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Waitall(2, rq, MPI_STATUSES_IGNORE);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
