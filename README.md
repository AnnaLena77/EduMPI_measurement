# EduMPI measurement

The measurement library of EduMPI Suite as an independent tool: it records MPI communication
through the standard interfaces PMPI and MPI_T (performance variables and events) instead of
a modified MPI library, and streams the records to EduStore (TimescaleDB) in near-real-time.

**Status: preparation phase.** The measurement tool itself is not written yet; what exists and was tested is in `docs/STATUS.md`. `tests/` holds the MPI_T probe, `cluster/` the Slurm job scripts for the Open MPI fork. The design and the work packages are in
`EDUMPI_T_DEVELOPMENT_PLAN.md` (sections 9, 16 "WP-G" and 17), kept next to this repository.

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
