/* time N MPI_T_init_thread / MPI_T_finalize cycles (no MPI_Init): cost of the MPI_T registration path */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 200, provided;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < n; ++i) {
        MPI_T_init_thread(MPI_THREAD_SINGLE, &provided);
        MPI_T_finalize();
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    printf("%d cycles: %.3f s, %.2f ms per cycle\n", n,
           (b.tv_sec - a.tv_sec) + 1e-9 * (b.tv_nsec - a.tv_nsec),
           1e3 * ((b.tv_sec - a.tv_sec) + 1e-9 * (b.tv_nsec - a.tv_nsec)) / n);
    return 0;
}
