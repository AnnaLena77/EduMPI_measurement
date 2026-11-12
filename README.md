# EduMPI measurement

The measurement library of EduMPI Suite as an independent tool: it records MPI communication
through the standard interfaces PMPI and MPI_T (performance variables and events) instead of
a modified MPI library, and streams the records to EduStore (TimescaleDB) in near-real-time.

**Status: working prototype.** The tool records MPI calls through PMPI and, where the library offers
them, MPI_T events, and writes either to EduStore (PostgreSQL binary `COPY`, same table and columns as
before) or to a per-rank binary file. What is built and how it was tested is in `docs/STATUS.md`; the
design and plan are in `EDUMPI_T_DEVELOPMENT_PLAN.md`, kept next to this repository.

## Use

```bash
make CC=mpicc                                   # libedumpi.so        (file sink)
make CC=mpicc PG=1 PQINC=<libpq include> PQLIB=<libpq lib>   # with the PostgreSQL sink
LD_PRELOAD=$PWD/libedumpi.so EDUMPI_LOG_DIR=/path mpirun -n 4 ./your_mpi_program
```

| Variable | Meaning |
|---|---|
| `EDUMPI_SINK` | `file`, `pg` or `both`; default `pg` if `DB_HOST` is set, else `file` |
| `EDUMPI_LOG_DIR` | directory of the per-rank files `edumpi.<rank>.bin` (`tools/edumpi_dump.py` prints them) |
| `DB_HOST DB_PORT DB_USER DB_NAME DB_PW`, `SLURM_JOBID`, `EDUMPI_PROGRAM` | as in EduMPI |
| `EDUMPI_MPIT=0` | PMPI only (also the behaviour on a library without the events) |
| `EDUMPI_FLUSH_MS` | flush interval of the monitor thread, default 500 |
| `EDUMPI_DISABLE=1` | do nothing |

The MPI_T data (collective algorithms and internal message sums, real completion times, late sender
and receiver) needs the Open MPI branches `mpit/pml-common-events` and `mpit/coll-events` of
`FuldaHPC/code-ompi_edumpi_t`; late sender/receiver needs `--mca pml ob1`.

## Related repositories

| Repository | Role |
|---|---|
| `FuldaHPC/code-ompi_edumpi_t` | Open MPI fork that adds the MPI_T events and variables this tool reads; they are meant to go upstream |
| `AnnaLena77/EduMPI` | The current EduMPI: an Open MPI fork with built-in measurement. Reference for behaviour and for the event store (branch `eduhpx_mpi_data`) |
| `AnnaLena77/edumpi_measurement_archive` | First MPI_T/PERUSE attempt on Open MPI 5.0.6, archived |
| `AnnaLena77/EduMPI_GUI` | Visualisation |

## `legacy/edumpi_measurement_archive/`

Unchanged copy of the directory `edumpi/` from `edumpi_measurement_archive`, branch
`measurement_tool`, commit `6a68e378db8df001a05e39e688990adfaab4e530` (May 2025): PMPI
wrappers for send, receive, sendrecv and blocking and non-blocking collectives, the
ring buffer with monitor thread, and the PostgreSQL binary COPY writer.

It is kept as material to port from, not as code to build:

- it reads Open MPI's monitoring PVARs and PERUSE callbacks (`peruse.h`), which the new
  design replaces with MPI_T events;
- it uses the ring buffer and the large `qentry` record, which the new design replaces with
  the segment store of `AnnaLena77/EduMPI`, branch `eduhpx_mpi_data`.
