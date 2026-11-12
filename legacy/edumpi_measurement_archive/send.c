#include <mpi.h>
#include <stdio.h>
#include "edumpi_measurement.h"

//blocking MPI_Send
int MPI_Send(const void *buf, int count, MPI_Datatype type, int dest,
             int tag, MPI_Comm comm)
{
     qentry *item = getWritingRingPos();
     item->callback = 1;
     clock_gettime(CLOCK_REALTIME, &item->start);
     initQentry(&item, dest, "MPI_Send", 8, count, 0, "p2p", 3, type, NULL, comm, 1, NULL);
     
     int result = PMPI_Send(buf, count, type, dest, tag, comm);
     clock_gettime(CLOCK_REALTIME, &item->end);
     item->callback = 0;
     
     return result;
}

//blocking ready send
int MPI_Rsend(const void *buf, int count, MPI_Datatype type, int dest, int tag, MPI_Comm comm)
{   
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Rsend", 9, count, 0, "p2p", 3, type, NULL, comm, 1, NULL);
    
    int result = PMPI_Rsend(buf, count, type, dest, tag, comm);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
     
    return result;
    
 }
 
 //blocking synchronous send
 int MPI_Ssend(const void *buf, int count, MPI_Datatype type, int dest, int tag, MPI_Comm comm)
{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Ssend", 9, count, 0, "p2p", 3, type, NULL, comm, 1, NULL);
    
    int result = PMPI_Ssend(buf, count, type, dest, tag, comm);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
    return result;
 
 }
 
 //blocking buffered send
 int MPI_Bsend(const void *buf, int count, MPI_Datatype type, int dest, int tag, MPI_Comm comm)
{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Bsend", 9, count, 0, "p2p", 3, type, NULL, comm, 1, NULL);
    
    int result = PMPI_Ssend(buf, count, type, dest, tag, comm);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
     
    return result;
}

//non-blocking MPI_Isend
int MPI_Isend(const void *buf, int count, MPI_Datatype type, int dest, int tag, MPI_Comm comm, MPI_Request *request)
{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Isend", 9, count, 0, "p2p", 3, type, NULL, comm, 0, NULL);
    item->request = request;
    
    int result = PMPI_Isend(buf, count, type, dest, tag, comm, request);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
     
    return result;
}

//non-blocking buffered send
int MPI_Ibsend(const void *buf, int count, MPI_Datatype type, int dest,
               int tag, MPI_Comm comm, MPI_Request *request)
{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Ibsend", 10, count, 0, "p2p", 3, type, NULL, comm, 0, NULL);
    
    int result = PMPI_Ibsend(buf, count, type, dest, tag, comm, request);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
     
    return result;
}

//non-blocking ready send
int MPI_Irsend(const void *buf, int count, MPI_Datatype type, int dest,
               int tag, MPI_Comm comm, MPI_Request *request)
{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Irsend", 10, count, 0, "p2p", 3, type, NULL, comm, 0, NULL);
   
    int result = PMPI_Irsend(buf, count, type, dest, tag, comm, request);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
     
    return result;
}

int MPI_Issend(const void *buf, int count, MPI_Datatype type, int dest,
               int tag, MPI_Comm comm, MPI_Request *request)
{
    qentry *item = getWritingRingPos();
    item->callback = 1;
    clock_gettime(CLOCK_REALTIME, &item->start);
    initQentry(&item, dest, "MPI_Issend", 10, count, 0, "p2p", 3, type, NULL, comm, 0, NULL);
    
    int result = PMPI_Issend(buf, count, type, dest, tag, comm, request);
    
    clock_gettime(CLOCK_REALTIME, &item->end);
    item->callback = 0;
     
    return result;
} 
   
 
