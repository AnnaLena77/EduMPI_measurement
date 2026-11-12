/* cost of the building blocks of a wrapped call (ns per operation) */
#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <stdatomic.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

int main(int argc, char **argv)
{
    int n = 2000000, sz;
    struct timespec ts;
    double a;
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    _Atomic unsigned long ctr = 0;
    MPI_Init(&argc, &argv);
    a = now(); for (int i = 0; i < n; ++i) clock_gettime(CLOCK_REALTIME, &ts);
    printf("clock_gettime(REALTIME)   %7.1f ns\n", (now() - a) / n);
    a = now(); for (int i = 0; i < n; ++i) clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("clock_gettime(MONOTONIC)  %7.1f ns\n", (now() - a) / n);
    a = now(); for (int i = 0; i < n; ++i) PMPI_Type_size(MPI_DOUBLE, &sz);
    printf("PMPI_Type_size            %7.1f ns\n", (now() - a) / n);
    a = now(); for (int i = 0; i < n; ++i) { pthread_mutex_lock(&m); pthread_mutex_unlock(&m); }
    printf("mutex lock+unlock         %7.1f ns\n", (now() - a) / n);
    a = now(); for (int i = 0; i < n; ++i) atomic_fetch_add(&ctr, 1);
    printf("atomic fetch_add          %7.1f ns\n", (now() - a) / n);
    {
        int rank, w;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        a = now(); for (int i = 0; i < n / 10; ++i) PMPI_Comm_rank(MPI_COMM_WORLD, &w);
        printf("PMPI_Comm_rank            %7.1f ns\n", (now() - a) / (n / 10));
    }
    MPI_Finalize();
    return 0;
}
