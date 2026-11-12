
#include <mpi.h>
#include <stdio.h>
#include "edumpi_measurement.h"

#define INIT_ENTRY(name, namelen, sendtype, recvtype, comm) \
    qentry *item = getWritingRingPos(); \
    clock_gettime(CLOCK_REALTIME, &item->start); \
    item->callback = 1; \
    initQentry(&item, -1, name, namelen, 0, 0, "collective", 10, sendtype, recvtype, comm, 1, NULL);

#define FINALIZE_ENTRY \
    clock_gettime(CLOCK_REALTIME, &item->end); \
    item->callback = 0;

int MPI_Barrier(MPI_Comm comm) {
    INIT_ENTRY("MPI_Barrier", 11, MPI_DATATYPE_NULL, MPI_DATATYPE_NULL, comm);
    int result = PMPI_Barrier(comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Bcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
    INIT_ENTRY("MPI_Bcast", 9, datatype, datatype, comm);
    int result = PMPI_Bcast(buffer, count, datatype, root, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Gather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
               void *recvbuf, int recvcount, MPI_Datatype recvtype,
               int root, MPI_Comm comm) {
    INIT_ENTRY("MPI_Gather", 7, sendtype, recvtype, comm);
    int result = PMPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Gatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                void *recvbuf, const int *recvcounts, const int *displs,
                MPI_Datatype recvtype, int root, MPI_Comm comm) {
    INIT_ENTRY("MPI_Gatherv", 11, sendtype, recvtype, comm);
    int result = PMPI_Gatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Scatter(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                void *recvbuf, int recvcount, MPI_Datatype recvtype,
                int root, MPI_Comm comm) {
    INIT_ENTRY("MPI_Scatter", 11, sendtype, recvtype, comm);
    int result = PMPI_Scatter(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Scatterv(const void *sendbuf, const int *sendcounts, const int *displs,
                 MPI_Datatype sendtype, void *recvbuf, int recvcount,
                 MPI_Datatype recvtype, int root, MPI_Comm comm) {
    INIT_ENTRY("MPI_Scatterv", 12, sendtype, recvtype, comm);
    int result = PMPI_Scatterv(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                  void *recvbuf, int recvcount, MPI_Datatype recvtype,
                  MPI_Comm comm) {
    INIT_ENTRY("MPI_Allgather", 13, sendtype, recvtype, comm);
    int result = PMPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Allgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                   void *recvbuf, const int *recvcounts, const int *displs,
                   MPI_Datatype recvtype, MPI_Comm comm) {
    INIT_ENTRY("MPI_Allgatherv", 14, sendtype, recvtype, comm);
    int result = PMPI_Allgatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Alltoall(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 void *recvbuf, int recvcount, MPI_Datatype recvtype,
                 MPI_Comm comm) {
    INIT_ENTRY("MPI_Alltoall", 12, sendtype, recvtype, comm);
    int result = PMPI_Alltoall(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Alltoallv(const void *sendbuf, const int *sendcounts, const int *sdispls,
                  MPI_Datatype sendtype, void *recvbuf, const int *recvcounts,
                  const int *rdispls, MPI_Datatype recvtype, MPI_Comm comm) {
    INIT_ENTRY("MPI_Alltoallv", 13, sendtype, recvtype, comm);
    int result = PMPI_Alltoallv(sendbuf, sendcounts, sdispls, sendtype,
                                recvbuf, recvcounts, rdispls, recvtype, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    INIT_ENTRY("MPI_Allreduce", 13, datatype, datatype, comm);
    int result = PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Reduce(const void *sendbuf, void *recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
    INIT_ENTRY("MPI_Reduce", 10, datatype, datatype, comm);
    int result = PMPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Reduce_scatter(const void *sendbuf, void *recvbuf, const int *recvcounts,
                       MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    INIT_ENTRY("MPI_Reduce_scatter", 18, datatype, datatype, comm);
    int result = PMPI_Reduce_scatter(sendbuf, recvbuf, recvcounts, datatype, op, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Reduce_scatter_block(const void *sendbuf, void *recvbuf, int recvcount,
                             MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    INIT_ENTRY("MPI_Reduce_scatter_block", 24, datatype, datatype, comm);
    int result = PMPI_Reduce_scatter_block(sendbuf, recvbuf, recvcount, datatype, op, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Scan(const void *sendbuf, void *recvbuf, int count,
             MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    INIT_ENTRY("MPI_Scan", 8, datatype, datatype, comm);
    int result = PMPI_Scan(sendbuf, recvbuf, count, datatype, op, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Exscan(const void *sendbuf, void *recvbuf, int count,
               MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
    INIT_ENTRY("MPI_Exscan", 10, datatype, datatype, comm);
    int result = PMPI_Exscan(sendbuf, recvbuf, count, datatype, op, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Neighbor_allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    INIT_ENTRY("MPI_Neighbor_allgather", 22, sendtype, recvtype, comm);
    int result = PMPI_Neighbor_allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
    FINALIZE_ENTRY;
    return result;
}

int MPI_Neighbor_alltoall(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                          void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
    INIT_ENTRY("MPI_Neighbor_alltoall", 21, sendtype, recvtype, comm);
    int result = PMPI_Neighbor_alltoall(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
    FINALIZE_ENTRY;
    return result;
}
