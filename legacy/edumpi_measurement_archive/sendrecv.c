#include <mpi.h>
#include <stdio.h>
#include "edumpi_measurement.h"

MPI_Datatype recv_type_;
MPI_Status s_;
MPI_Status *status_helper_;

int MPI_Sendrecv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 int dest, int sendtag, void *recvbuf, int recvcount,
                 MPI_Datatype recvtype, int source, int recvtag,
                 MPI_Comm comm,  MPI_Status *status)
{
    recv_type_ = recvtype;
    
    if (status == MPI_STATUS_IGNORE) {
        //printf("MPI_STATUS_IGNORE\n");
        status_helper_ = &s_;
    } else {
        status_helper_ = status;
    }
    
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Sendrecv", 12, 0, 0, "p2p", 3, sendtype, recvtype, comm, 1, NULL);
    
    int result = PMPI_Sendrecv(sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, status_helper_);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
    
    int recvtype_size = 0;
    MPI_Type_size(recv_type_, &recvtype_size); 
    int received_count;
    MPI_Get_count(status_helper_, MPI_INT, &received_count);

    item->recvcount = received_count;
    item->recvDatasize = received_count * recvtype_size;
    
    return result;
    
}

int MPI_Sendrecv_replace(void * buf, int count, MPI_Datatype datatype,
                         int dest, int sendtag, int source, int recvtag,
                         MPI_Comm comm, MPI_Status *status)

{
   recv_type_ = datatype;
    
    if (status == MPI_STATUS_IGNORE) {
        //printf("MPI_STATUS_IGNORE\n");
        status_helper_ = &s_;
    } else {
        status_helper_ = status;
    }

    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Sendrecv_replace", 20, 0, 0, "p2p", 3, datatype, datatype, comm, 1, NULL);
    
    int result = PMPI_Sendrecv_replace(buf, count, datatype, dest, sendtag, source, recvtag, comm, status_helper_);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
    
    int recvtype_size = 0;
    MPI_Type_size(recv_type_, &recvtype_size); 
    int received_count;
    MPI_Get_count(status_helper_, MPI_INT, &received_count);

    item->recvcount = received_count;
    item->recvDatasize = received_count * recvtype_size;
    
    return result;
}

int MPI_Isendrecv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 int dest, int sendtag, void *recvbuf, int recvcount,
                 MPI_Datatype recvtype, int source, int recvtag,
                 MPI_Comm comm,  MPI_Request *request)
{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Isendrecv", 13, 0, 0, "p2p", 3, sendtype, recvtype, comm, 0, NULL);
    
    int result = PMPI_Isendrecv(sendbuf, sendcount, sendtype, dest, sendtag, recvbuf, recvcount, recvtype, source, recvtag, comm, request);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
    
    return result;
}

int MPI_Isendrecv_replace(void * buf, int count, MPI_Datatype datatype,
                         int dest, int sendtag, int source, int recvtag,
                         MPI_Comm comm, MPI_Request *request)

{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Isendrecv_replace", 21, 0, 0, "p2p", 3, datatype, datatype, comm, 0, NULL);
    
    int result = PMPI_Isendrecv_replace(buf, count, datatype, dest, sendtag, source, recvtag, comm, request);

    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
    
    return result;
}
