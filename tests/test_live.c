/* Slow program with few MPI calls (like a teaching example): one ring exchange and a barrier per second, 8 seconds,
   plus a receive that stays posted for the whole run. A reader of the database must see rows long before the end. */
#include <mpi.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int rank, size, dummy = 0, in = 0, out;
    MPI_Request idle;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Irecv(&dummy, 1, MPI_INT, MPI_ANY_SOURCE, 999, MPI_COMM_WORLD, &idle);   /* stays open until the end */
    for (int i = 0; i < 8; ++i) {
        out = i;
        MPI_Sendrecv(&out, 1, MPI_INT, (rank + 1) % size, 1, &in, 1, MPI_INT, (rank + size - 1) % size, 1, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
        MPI_Barrier(MPI_COMM_WORLD);
        sleep(1);
    }
    MPI_Cancel(&idle);
    MPI_Wait(&idle, MPI_STATUS_IGNORE);
    MPI_Finalize();
    return 0;
}
