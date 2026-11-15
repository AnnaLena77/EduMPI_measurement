/* Prints the number of threads of the process (Linux /proc/self/task) after MPI_Init and after some calls: with
   EDUMPI_THREAD=0 the tool must not add one. Run with 2 ranks. */
#include <dirent.h>
#include <mpi.h>
#include <stdio.h>

static int nthreads(void)
{
    int n = 0;
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d))) if (e->d_name[0] != '.') ++n;
    closedir(d);
    return n;
}

int main(int argc, char **argv)
{
    int rank, v = 1, w = 0;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    for (int i = 0; i < 5000; ++i) {          /* more than one segment of records */
        MPI_Allreduce(&v, &w, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    }
    if (0 == rank) printf("THREADS %d\n", nthreads());
    MPI_Finalize();
    return 0;
}
