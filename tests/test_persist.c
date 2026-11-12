/* Persistent requests (4 ranks): a persistent send/receive ring started 5 times, and a persistent Allreduce started
   3 times. Every MPI_Start must produce one record with the bytes and partner of the request, completed by the wait. */
#include <mpi.h>
#include <string.h>

int main(int argc, char **argv)
{
    int rank, n, i;
    char sbuf[100], rbuf[100];
    double a[10] = {0}, ar[10];
    MPI_Request pr[2], cr;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n);
    memset(sbuf, rank, sizeof(sbuf));
    MPI_Recv_init(rbuf, 100, MPI_CHAR, (rank + n - 1) % n, 9, MPI_COMM_WORLD, &pr[0]);
    MPI_Send_init(sbuf, 100, MPI_CHAR, (rank + 1) % n, 9, MPI_COMM_WORLD, &pr[1]);
    for (i = 0; i < 5; ++i) {
        MPI_Startall(2, pr);
        MPI_Waitall(2, pr, MPI_STATUSES_IGNORE);
    }
    MPI_Request_free(&pr[0]);
    MPI_Request_free(&pr[1]);
    MPI_Allreduce_init(a, ar, 10, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD, MPI_INFO_NULL, &cr);
    for (i = 0; i < 3; ++i) {
        MPI_Start(&cr);
        MPI_Wait(&cr, MPI_STATUS_IGNORE);
    }
    MPI_Request_free(&cr);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
