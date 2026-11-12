#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "edumpi_measurement.h"

static int comm_world_size;
static int comm_world_rank;

static void init_monitoring_pvar(const char *, monitoring_pvar *);
static void start_monitoring_pvar(monitoring_pvar *);
static void stop_monitoring_pvar(monitoring_pvar *);

int MPI_Init(int* argc, char*** argv)
{
    int result, provided;
    int MPIT_result;

    result = PMPI_Init(argc, argv);
    
    PMPI_Comm_size(MPI_COMM_WORLD, &comm_world_size);
    PMPI_Comm_rank(MPI_COMM_WORLD, &comm_world_rank);
    
    MPIT_result = MPI_T_init_thread(MPI_THREAD_SINGLE, &provided);
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "ERROR : failed to initialize MPI_T interface.\n");
        PMPI_Abort(MPI_COMM_WORLD, MPIT_result);
    }

    MPIT_result = MPI_T_pvar_session_create(&session);
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "ERROR : failed to create MPI_T session.\n");
        PMPI_Abort(MPI_COMM_WORLD, MPIT_result);
    }
    
    init_monitoring_pvar("pml_monitoring_messages_count", &pml_counts);
    init_monitoring_pvar("pml_monitoring_messages_size", &pml_sizes);
    init_monitoring_pvar("osc_monitoring_messages_sent_count", &osc_scounts);
    init_monitoring_pvar("osc_monitoring_messages_sent_size", &osc_ssizes);
    init_monitoring_pvar("osc_monitoring_messages_recv_count", &osc_rcounts);
    init_monitoring_pvar("osc_monitoring_messages_recv_size", &osc_rsizes);
    init_monitoring_pvar("coll_monitoring_messages_count", &coll_counts);
    init_monitoring_pvar("coll_monitoring_messages_size", &coll_sizes);
    
    start_monitoring_pvar(&pml_counts);
    start_monitoring_pvar(&pml_sizes);
    start_monitoring_pvar(&osc_scounts);
    start_monitoring_pvar(&osc_ssizes);
    start_monitoring_pvar(&osc_rcounts);
    start_monitoring_pvar(&osc_rsizes);
    start_monitoring_pvar(&coll_counts);
    start_monitoring_pvar(&coll_sizes);
    
    
    initialize_thread_for_rank(comm_world_rank, comm_world_size);
    initialize_peruse_callbacks();
    return result;
}


int MPI_Finalize(void){
    
    int result, MPIT_result;
    
    stop_monitoring_pvar(&pml_counts);
    stop_monitoring_pvar(&pml_sizes);
    stop_monitoring_pvar(&osc_scounts);
    stop_monitoring_pvar(&osc_ssizes);
    stop_monitoring_pvar(&osc_rcounts);
    stop_monitoring_pvar(&osc_rsizes);
    stop_monitoring_pvar(&coll_counts);
    stop_monitoring_pvar(&coll_sizes);

    MPIT_result = MPI_T_pvar_session_free(&session);
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "WARNING : failed to free MPI_T session, monitoring results may be impacted : check your OpenMPI installation\n");
    }

    MPIT_result = MPI_T_finalize();
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "WARNING : failed to finalize MPI_T interface, monitoring results may be impacted : check your OpenMPI installation\n");
    }

    finalize_thread_for_rank();
    int res = PMPI_Finalize();
    
    return res;
}

void init_monitoring_pvar(const char * pvar_name, monitoring_pvar * res)
{
    int count = 0;
    int MPIT_result;
    MPI_Comm comm_world = MPI_COMM_WORLD;

    res->pvar_name = strdup(pvar_name);

    MPIT_result = MPI_T_pvar_get_index(res->pvar_name, MPI_T_PVAR_CLASS_SIZE, &(res->pvar_idx));
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "ERROR : cannot find monitoring MPI_T \"%s\" pvar, check that you have monitoring pml\n", pvar_name);
        PMPI_Abort(MPI_COMM_WORLD, MPIT_result);
    }
 
    MPIT_result = MPI_T_pvar_handle_alloc(session, res->pvar_idx, &comm_world, &(res->pvar_handle), &count);
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "ERROR : failed to allocate handle on \"%s\" pvar, check that you have monitoring pml\n", pvar_name);
        PMPI_Abort(MPI_COMM_WORLD, MPIT_result);
    }

    if (count != comm_world_size) {
        fprintf(stderr, "ERROR : COMM_WORLD has %d ranks \"%s\" pvar contains %d values, check that you have monitoring pml\n", comm_world_size, pvar_name, count);
        PMPI_Abort(MPI_COMM_WORLD, count);
    } 

    res->start = (size_t *) malloc(comm_world_size * sizeof(size_t));
    res->end = (size_t *) malloc(comm_world_size * sizeof(size_t));
}

void start_monitoring_pvar(monitoring_pvar * res)
{
    int MPIT_result;

    MPIT_result = MPI_T_pvar_start(session, res->pvar_handle);
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "ERROR : failed to start handle on \"%s\" pvar, check that you have enabled the monitoring pml\n", res->pvar_name);
        PMPI_Abort(MPI_COMM_WORLD, MPIT_result);
    }
}

void stop_monitoring_pvar(monitoring_pvar * res)
{
    int MPIT_result;

    MPIT_result = MPI_T_pvar_stop(session, res->pvar_handle);
    if (MPIT_result != MPI_SUCCESS) {
        fprintf(stderr, "ERROR : failed to stop handle on \"%s\" pvar, check that you have enabled the monitoring pml\n", res->pvar_name);
        MPI_Abort(MPI_COMM_WORLD, MPIT_result);
    }
}
