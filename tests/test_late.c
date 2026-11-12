/* Late sender / late receiver with known delays (2 ranks, pml ob1).
   Scenario A: rank 1 receives 8 bytes while rank 0 sleeps 0.3 s before sending      -> late sender 0.3 s at rank 1
   Scenario B: rank 0 sends 4 MiB (rendezvous) while rank 1 sleeps 0.3 s before the receive -> late receiver 0.3 s at rank 0
   Scenario C: rank 0 sends 8 bytes (eager) while rank 1 sleeps 0.3 s                  -> no late receiver */
#include <mpi.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int rank;
    char small[8] = {0};
    char *big = malloc(4 << 20);
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Barrier(MPI_COMM_WORLD);
    /* A */
    if (0 == rank) { usleep(300000); MPI_Send(small, 8, MPI_CHAR, 1, 1, MPI_COMM_WORLD); }
    else if (1 == rank) { MPI_Recv(small, 8, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE); }
    MPI_Barrier(MPI_COMM_WORLD);
    /* B */
    if (0 == rank) { MPI_Send(big, 4 << 20, MPI_CHAR, 1, 2, MPI_COMM_WORLD); }
    else if (1 == rank) { usleep(300000); MPI_Recv(big, 4 << 20, MPI_CHAR, 0, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE); }
    MPI_Barrier(MPI_COMM_WORLD);
    /* C */
    if (0 == rank) { MPI_Send(small, 8, MPI_CHAR, 1, 3, MPI_COMM_WORLD); }
    else if (1 == rank) { usleep(300000); MPI_Recv(small, 8, MPI_CHAR, 0, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE); }
    MPI_Barrier(MPI_COMM_WORLD);
    free(big);
    MPI_Finalize();
    return 0;
}
