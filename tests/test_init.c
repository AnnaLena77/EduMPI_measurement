/* plain MPI program (no tool code in it): the tool is injected with LD_PRELOAD */
#include <mpi.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    int rank;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    printf("rank %d done\n", rank);
    return 0;
}
