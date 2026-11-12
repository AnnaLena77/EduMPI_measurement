#include <mpi.h>
#include <stdio.h>
#include "edumpi_measurement.h"


#include <mpi.h>
#include <stdio.h>
#include "edumpi_measurement.h"

#define START_MEASURE(fname, length, senddatatype, recvdatatype, comm, req) \
    qentry *item = getWritingRingPos(); \
    item->callback = 1; \
    clock_gettime(CLOCK_REALTIME, &item->start); \
    item->request = req; \
    initQentry(&item, -1, fname, length, 0, 0, "collective", 10, senddatatype, recvdatatype, comm, 0, NULL);

#define END_MEASURE \
    clock_gettime(CLOCK_REALTIME, &item->end); \
    item->callback = 0;

int MPI_Ibarrier(MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ibarrier", 10, NULL, NULL, comm, request)
    int result = PMPI_Ibarrier(comm, request);
    END_MEASURE
    return result;
}

int MPI_Ibcast(void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ibcast", 10, datatype, NULL, comm, request)
    int result = PMPI_Ibcast(buffer, count, datatype, root, comm, request);
    END_MEASURE
    return result;
}

int MPI_Igather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                void *recvbuf, int recvcount, MPI_Datatype recvtype,
                int root, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Igather", 11, sendtype, recvtype, comm, request)
    int result = PMPI_Igather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm, request);
    END_MEASURE
    return result;
}

int MPI_Igatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 void *recvbuf, const int *recvcounts, const int *displs,
                 MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Igatherv", 12, sendtype, recvtype, comm, request)
    int result = PMPI_Igatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, root, comm, request);
    END_MEASURE
    return result;
}

int MPI_Iscatter(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 void *recvbuf, int recvcount, MPI_Datatype recvtype,
                 int root, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Iscatter", 12, sendtype, recvtype, comm, request)
    int result = PMPI_Iscatter(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, comm, request);
    END_MEASURE
    return result;
}

int MPI_Iscatterv(const void *sendbuf, const int *sendcounts, const int *displs,
                  MPI_Datatype sendtype, void *recvbuf, int recvcount,
                  MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Iscatterv", 13, sendtype, recvtype, comm, request)
    int result = PMPI_Iscatterv(sendbuf, sendcounts, displs, sendtype, recvbuf, recvcount, recvtype, root, comm, request);
    END_MEASURE
    return result;
}

int MPI_Iallgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                   void *recvbuf, int recvcount, MPI_Datatype recvtype,
                   MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Iallgather", 15, sendtype, recvtype, comm, request)
    int result = PMPI_Iallgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, request);
    END_MEASURE
    return result;
}

int MPI_Iallgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                    void *recvbuf, const int *recvcounts, const int *displs,
                    MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Iallgatherv", 16, sendtype, recvtype, comm, request)
    int result = PMPI_Iallgatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, request);
    END_MEASURE
    return result;
}

int MPI_Ialltoall(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                  void *recvbuf, int recvcount, MPI_Datatype recvtype,
                  MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ialltoall", 14, sendtype, recvtype, comm, request)
    int result = PMPI_Ialltoall(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, request);
    END_MEASURE
    return result;
}

int MPI_Ialltoallv(const void *sendbuf, const int *sendcounts, const int *sdispls, MPI_Datatype sendtype,
                   void *recvbuf, const int *recvcounts, const int *rdispls, MPI_Datatype recvtype,
                   MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ialltoallv", 15, sendtype, recvtype, comm, request)
    int result = PMPI_Ialltoallv(sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls, recvtype, comm, request);
    END_MEASURE
    return result;
}

int MPI_Ialltoallw(const void *sendbuf, const int sendcounts[], const int sdispls[],
                   const MPI_Datatype sendtypes[], void *recvbuf, const int recvcounts[],
                   const int rdispls[], const MPI_Datatype recvtypes[], MPI_Comm comm,
                   MPI_Request *request) {
    START_MEASURE("MPI_Ialltoallw", 15, sendtypes[0], recvtypes[0], comm, request)   
    int result = PMPI_Ialltoallw(sendbuf, sendcounts, sdispls, sendtypes, recvbuf, recvcounts, rdispls, recvtypes, comm, request);
    END_MEASURE
    return result;
}
             

int MPI_Ireduce(const void *sendbuf, void *recvbuf, int count,
                MPI_Datatype datatype, MPI_Op op, int root,
                MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ireduce", 11, datatype, datatype, comm, request)
    int result = PMPI_Ireduce(sendbuf, recvbuf, count, datatype, op, root, comm, request);
    END_MEASURE
    return result;
}

int MPI_Iallreduce(const void *sendbuf, void *recvbuf, int count,
                   MPI_Datatype datatype, MPI_Op op,
                   MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Iallreduce", 14, datatype, datatype, comm, request)
    int result = PMPI_Iallreduce(sendbuf, recvbuf, count, datatype, op, comm, request);
    END_MEASURE
    return result;
}

int MPI_Ireduce_scatter(const void *sendbuf, void *recvbuf, const int recvcounts[],
                        MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ireduce_scatter", 19, datatype, datatype, comm, request)
    int result = PMPI_Ireduce_scatter(sendbuf, recvbuf, recvcounts, datatype, op, comm, request);
    END_MEASURE
    return result;                        
}

int MPI_Ireduce_scatter_block(const void *sendbuf, void *recvbuf, int recvcount,
                              MPI_Datatype datatype, MPI_Op op,
                              MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ireduce_scatter_block", 25, datatype, datatype, comm, request)
    int result = PMPI_Ireduce_scatter_block(sendbuf, recvbuf, recvcount, datatype, op, comm, request);
    END_MEASURE
    return result;                        
}                              

                           
int MPI_Iscan(const void *sendbuf, void *recvbuf, int count,
              MPI_Datatype datatype, MPI_Op op,
              MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Iscan", 6, datatype, datatype, comm, request)
    int result = PMPI_Iscan(sendbuf, recvbuf, count, datatype, op, comm, request);
    END_MEASURE
    return result;
}

int MPI_Iexscan(const void *sendbuf, void *recvbuf, int count,
                MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Iexscan", 11, datatype, datatype, comm, request)
    int result = PMPI_Iexscan(sendbuf, recvbuf, count, datatype, op, comm, request);
    END_MEASURE
    return result;         
}

int MPI_Ineighbor_allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                            void *recvbuf, int recvcount, MPI_Datatype recvtype,
                            MPI_Comm comm,  MPI_Request *request) {
    START_MEASURE("MPI_Ineighbor_allgather", 23, sendtype, recvtype, comm, request)
    int result = PMPI_Ineighbor_allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, request);
    END_MEASURE
    return result;         
}                           
                           

int MPI_Ineighbor_allgatherv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                             void *recvbuf, const int recvcounts[], const int displs[],
                             MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ineighbor_allgatherv", 24, sendtype, recvtype, comm, request)
    int result = PMPI_Ineighbor_allgatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, request);
    END_MEASURE
    return result;  
}                              
                             

int MPI_Ineighbor_alltoall(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                           void *recvbuf, int recvcount, MPI_Datatype recvtype,
                           MPI_Comm comm, MPI_Request *request) {
    START_MEASURE("MPI_Ineighbor_alltoall", 22, sendtype, recvtype, comm, request)
    int result = PMPI_Ineighbor_alltoall(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm, request);
    END_MEASURE
    return result;  
}                                  
                           

int MPI_Ineighbor_alltoallv(const void *sendbuf, const int sendcounts[], const int sdispls[],
                            MPI_Datatype sendtype, void *recvbuf, const int recvcounts[],
                            const int rdispls[], MPI_Datatype recvtype, MPI_Comm comm,
                            MPI_Request *request) {
    START_MEASURE("MPI_Ineighbor_alltoallv", 23, sendtype, recvtype, comm, request)
    int result = PMPI_Ineighbor_alltoallv(sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls, recvtype, comm, request);
    END_MEASURE
    return result;  
}                              
                           

int MPI_Ineighbor_alltoallw(const void *sendbuf, const int sendcounts[], const MPI_Aint sdispls[],
                            const MPI_Datatype sendtypes[], void *recvbuf, const int recvcounts[],
                            const MPI_Aint rdispls[], const MPI_Datatype recvtypes[], MPI_Comm comm,
                            MPI_Request *request) {
    START_MEASURE("MPI_Ineighbor_alltoallw", 23, sendtypes[0], recvtypes[0], comm, request)
    int result = PMPI_Ineighbor_alltoallw(sendbuf, sendcounts, sdispls, sendtypes, recvbuf, recvcounts, rdispls, recvtypes, comm, request);
    END_MEASURE
    return result;  
}                             


