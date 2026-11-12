#include <mpi.h>
#include <stdio.h>
#include "edumpi_measurement.h"

MPI_Datatype recv_type;
MPI_Status s;
MPI_Status *status_helper;

int MPI_Recv(void *buf, int count, MPI_Datatype type, int source, int tag, MPI_Comm comm, MPI_Status *status)
{

    recv_type = type;
    
    if (status == MPI_STATUS_IGNORE) {
        //printf("MPI_STATUS_IGNORE\n");
        status_helper = &s;
    } else {
        status_helper = status;
    }
    
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, source, "MPI_Recv", 8, 0, 0, "p2p", 3, NULL, type, comm, 1, NULL);
    
    int result = PMPI_Recv(buf, count, type, source, tag, comm, status_helper);
    clock_gettime(CLOCK_REALTIME, &item->end);
    
    int recvtype_size = 0;
    MPI_Type_size(recv_type, &recvtype_size); 
    int received_count;
    int tes = MPI_Get_count(status_helper, MPI_INT, &received_count);

    item->recvcount = received_count;
    item->recvDatasize = received_count * recvtype_size;
    item->callback = 0;
    return result;
}

int MPI_Irecv(void *buf, int count, MPI_Datatype type, int source,
              int tag, MPI_Comm comm, MPI_Request *request)
{
    /*recv_type = type;
    
    if (status == MPI_STATUS_IGNORE) {
        //printf("MPI_STATUS_IGNORE\n");
        status_helper = &s;
    } else {
        status_helper = status;
    }*/
    
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, source, "MPI_Irecv", 9, 0, 0, "p2p", 3, NULL, type, comm, 0, NULL);
    item->request = request;
    
    int result = PMPI_Irecv(buf, count, type, source, tag, comm, request);

    /*int recvtype_size = 0;
    MPI_Type_size(recv_type, &recvtype_size); 
    int received_count;
    int tes = MPI_Get_count(status_helper, MPI_INT, &received_count);

    item->recvcount = received_count;
    item->recvDatasize = received_count * recvtype_size;*/
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
    return result;
}

int MPI_Mrecv(void *buf, int count, MPI_Datatype type,
              MPI_Message *message, MPI_Status *status)
{
    recv_type = type;
    
    if (status == MPI_STATUS_IGNORE) {
        //printf("MPI_STATUS_IGNORE\n");
        status_helper = &s;
    } else {
        status_helper = status;
    }
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, -1, "MPI_Mrecv", 9, 0, 0, "p2p", 3, NULL, type, NULL, 1, NULL);

    int result = PMPI_Mrecv(buf, count, type, message, status);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    
    int recvtype_size = 0;
    MPI_Type_size(recv_type, &recvtype_size); 
    int received_count;
    int tes = MPI_Get_count(status_helper, MPI_INT, &received_count);

    item->recvcount = received_count;
    item->recvDatasize = received_count * recvtype_size;
    item->callback = 0;
    return result;
}

int MPI_Imrecv(void *buf, int count, MPI_Datatype type,
               MPI_Message *message, MPI_Request *request)
{
    /*recv_type = type;
    
    if (status == MPI_STATUS_IGNORE) {
        //printf("MPI_STATUS_IGNORE\n");
        status_helper = &s;
    } else {
        status_helper = status;
    }*/
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, -1, "MPI_Imrecv", 10, 0, 0, "p2p", 3, type, NULL, NULL, 0, NULL);
    item->request = request;
    
    int result = PMPI_Imrecv(buf, count, type, message, request);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    
    /*int recvtype_size = 0;
    MPI_Type_size(recv_type, &recvtype_size); 
    int received_count;
    int tes = MPI_Get_count(status_helper, MPI_INT, &received_count);

    item->recvcount = received_count;
    item->recvDatasize = received_count * recvtype_size;*/
    item->callback = 0;
    return result;
}


