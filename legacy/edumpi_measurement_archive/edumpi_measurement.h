#ifndef EDUMPI_MEASUREMENT_H
#define EDUMPI_MEASUREMENT_H

#include <sys/queue.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdint.h>
#include <mpi.h>

#define MAX_RINGSIZE 1000000


extern void enqueue(char** operation, char** datatype, int count, int datasize, char** communicator, int processrank, int partnerrank, time_t ctime);
extern void initializeMongoDB(void);
extern void closeMongoDB(void);
extern pthread_t MONITOR_THREAD;
extern int run_thread;
extern int counter;

void test_function(void);
void initialize_thread_for_rank(int rank, int world_size);
void finalize_thread_for_rank(void);

void initialize_peruse_callbacks(void);

typedef struct qentry {
    int id;
    char function[30];
    char communicationType[30];
    int blocking;
    char sendDatatype[64];
    char recvDatatype[64];
    int sendcount;
    int sendDatasize;
    int recvcount;
    int recvDatasize;
    char operation[30]; //MPI_Reduce, MPI_Accumulate
    char communicationArea[64];
    MPI_Comm communicator;
    char processorname[30];
    int processrank;
    int partnerrank;
    uint8_t coll_partnerranks[50];
    int callback;
    int callback_;
    double req_activate_time;
    double req_xfer_time;
    //char sendmode[30]; //later
    //int immediate; //later
    //char usedBtl[30];
    //char usedProtocol[30];
    //int withinEagerLimit;
    //int foundMatchWild;
    char usedAlgorithm[30];
    MPI_Request *request;
    struct timespec start;
    struct timespec end;
    double lateSenderTime;
    double lateReceiverTime;
    double sendWaitingTime;
    double recvWaitingTime;
    //struct timeval start;
    //struct timespec initializeRequest;
    //struct timespec startRequest;
    //Completion of the first fragment of a long message that requires an acknowledgement
    //struct timespec requestCompletePmlLevel;
    //Warten auf Recv-Request
    //struct timespec requestWaitCompletion;
    //struct timespec requestFini;
    //struct timespec sent;//later
    //struct timespec bufferFree; //later
    //struct collective_p2p collectives;
    TAILQ_ENTRY(qentry) pointers;
} qentry;

extern void qentryIntoQueue(qentry **q);
extern void initQentry(qentry **q, int dest, char *function, int function_len, int sendCount, int recvCount, char *commType, int commType_len, MPI_Datatype sendType, MPI_Datatype recvType, MPI_Comm comm, int blocking, MPI_Op op);
extern double timespec_diff(struct timespec start, struct timespec end);
//extern void writeIntoFile(qentry q);

extern qentry *q_qentry;

extern qentry* getWritingRingPos(void);

extern qentry *ringbuffer;
extern int writer_pos;
extern int reader_pos;

extern void closeFile(void);

/*
   Structs for initializing PVARS (measurement using MPI_T), code based on monitoring_prof.c, Open MPI
*/

struct monitoring_pvar
{
    char * pvar_name;
    int pvar_idx;
    MPI_T_pvar_handle pvar_handle;
    size_t * start;
    size_t * end;
};
typedef struct monitoring_pvar monitoring_pvar;

/* PML Sent */
static monitoring_pvar pml_counts;
static monitoring_pvar pml_sizes;
/* OSC Sent */
static monitoring_pvar osc_scounts;
static monitoring_pvar osc_ssizes;
/* OSC Recv */
static monitoring_pvar osc_rcounts;
static monitoring_pvar osc_rsizes;
/* COLL Sent/Recv */
static monitoring_pvar coll_counts;
static monitoring_pvar coll_sizes;

static MPI_T_pvar_session session;

#endif // EDUMPI_MEASUREMENT_H

