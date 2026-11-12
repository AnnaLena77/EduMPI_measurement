/*
 * PMPI wrappers for MPI_Init, MPI_Init_thread and MPI_Finalize: start and stop the event store
 * and the monitor thread. Loaded with LD_PRELOAD (libedumpi.so) or linked into the program.
 *
 * Environment: EDUMPI_LOG_DIR (default "."), EDUMPI_FLUSH_MS (default 500), EDUMPI_DISABLE=1.
 */
#include <mpi.h>
#include <stdlib.h>

#include "wrap.h"


static void start_tool(void)
{
    int rank = 0, size = 1, len = 0;
    char node[MPI_MAX_PROCESSOR_NAME] = "";
    const char *dir = getenv("EDUMPI_LOG_DIR");
    const char *ms = getenv("EDUMPI_FLUSH_MS");
    const char *off = getenv("EDUMPI_DISABLE");

    if (off && '1' == off[0]) {
        return;
    }
    PMPI_Comm_rank(MPI_COMM_WORLD, &rank);
    PMPI_Comm_size(MPI_COMM_WORLD, &size);
    PMPI_Get_processor_name(node, &len);
    edumpi_store_init(rank, size, node, edumpi_file_sink_new(dir ? dir : "."), ms ? (unsigned) atoi(ms) : 500);
    edumpi_enabled = 1;
}

int MPI_Init(int *argc, char ***argv)
{
    int rc = PMPI_Init(argc, argv);
    if (MPI_SUCCESS == rc) {
        start_tool();
    }
    return rc;
}

int MPI_Init_thread(int *argc, char ***argv, int required, int *provided)
{
    int rc = PMPI_Init_thread(argc, argv, required, provided);
    if (MPI_SUCCESS == rc) {
        start_tool();
    }
    return rc;
}

int MPI_Finalize(void)
{
    if (edumpi_enabled) {
        edumpi_enabled = 0;
        edumpi_store_finalize();
    }
    return PMPI_Finalize();
}
