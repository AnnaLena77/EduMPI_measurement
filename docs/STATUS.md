# Status: what is done and what was tested

Last updated 2026-11-12. This page records only what was actually built and run. The design
and the full plan are in `EDUMPI_T_DEVELOPMENT_PLAN.md` (kept next to this repository).

## 1. Overview

| Part | State |
|---|---|
| Open MPI fork `FuldaHPC/code-ompi_edumpi_t` (base: upstream `main`, 6.1.0a1, commit `89b41f785f`) | builds (release and debug); WP-A on branch `mpit/pml-common-events` and WP-B on branch `mpit/coll-events` (stacked on WP-A), both implemented, pushed to the fork and tested |
| This tool (`EduMPI_measurement`) | milestones G1 (store, file sink), G2 (70 PMPI wrappers), G3 (MPI_T backend: completion, internal sums, bitmaps, algorithms) G4 (late sender/receiver on ob1) and G1b (PostgreSQL sink) done; G5, G6 open |
| Upstream pull requests | none (decision: prototype first) |

## 2. Test environment

FuldaHPC cluster (NixOS, Slurm partition `vip`), nodes `compute-00`/`compute-01`, system
Open MPI 5.0.9 and UCX 1.19.0 (UCX built without statistics). The fork is built with
internal PMIx/hwloc/PRRTE, `--disable-mpi-fortran`; release (`main-rel`) and
`--enable-debug` (`main-dbg`, `dev-dbg`). Builds run on node-local `/tmp` (one-off) or in a
persistent tree on `/data` (development); results are in `/data/fdai0231/edumpi-t/`. The job
scripts are in `cluster/`.

## 3. Results

### 3.1 Upstream baseline (WP-0)

| Check | Result | Job / script |
|---|---|---|
| Release build of upstream `main` | OK, 40 min; 27 MPI_T events, 18 of them `ompi.pml_ob1_*` | 10351, `cluster/build.sbatch` |
| Debug build and `ompi/test/t` | OK, 25/25 pass | 10361, `cluster/build_check.sbatch` |
| `test/util` (includes the event engine test `mca_base_event_test`) | 33/33 pass | 10372, `cluster/util_check.sbatch` |
| Existing ob1 events, 2 ranks, probe `tests/mpit_ob1_probe.c` | see 3.2 | 10357 (tcp, 2 nodes), 10360 (sm, 1 node), 10359 (ucx) |

### 3.2 What the existing ob1 events show (measured, one-second delay)

- **Late sender** works with existing events: `request_activate` to `posted_match` of the same
  request token = 1000.47 ms.
- **Late receiver, receiver side:** `unexpected_insert` to `unexpected_match` (same fragment
  token) = 1000.18 ms.
- **Late receiver, sender side (rendezvous, 4 MiB):** `request_activate` to the first
  `transfer` event = 1000.39 ms (`transfer_begin` fires immediately).
- Eager and rendezvous differ in the event pattern. Internal messages of collectives are
  recognisable by negative tags (barrier = −16).
- **Gap found:** the request events carry no peer and tag; over `btl tcp` a send's peer and tag
  appear in no event. Under `pml ucx` no ob1 event is raised at all. This is what WP-A closes.
- Event timestamps are per process; only differences between events of the same rank are valid.

### 3.3 WP-A: PML independent point-to-point events (branch `mpit/pml-common-events`)

Four events, bound to a communicator, source `ompi`, level 4:

| Event | Payload |
|---|---|
| `ompi.pml_send_start`, `ompi.pml_recv_start` | request (u64 token), parent (u64, reserved 0), length (i64), peer (i32, `MPI_ANY_SOURCE` possible), peer_world (i32), tag (i32, negative = internal collective message), flags (i32: 1 sync, 2 ready, 4 buffered, 8 blocking, 16 persistent) |
| `ompi.pml_send_complete`, `ompi.pml_recv_complete` | request, length (bytes transferred), peer (real source for a receive), peer_world, tag, flags (1 cancelled, 2 error, 4 truncated) |

Implementation: `ompi/mca/pml/base/pml_base_events.[ch]`; raise sites in `pml/ob1` (next to
the existing activate/complete events and the inline-send path) and `pml/ucx`.

| Test | Result |
|---|---|
| New unit test `ompi/test/t/mpi_t_event_pml_p2p.c` (non-blocking pair, any-source, synchronous blocking send; checks peer, tag, bytes, flags), `pml ob1` and `pml ucx`, singleton and `mpirun -n 1` | 16/16 checks pass in all four runs (job 10371) |
| 2-node probe of the four events, both PMLs | events delivered; internal collective messages visible with tags −12 and −16 |
| Bug found by the probe and fixed | UCX receive tags were read unsigned (16777204 instead of −12) |
| Debug build with the changes | builds without errors (job 10370) |

Known limits (also in the plan): under `pml ucx` a non-blocking send raises `send_start` but no
`send_complete` (the UCX completion callback knows neither communicator nor peer; WP-U);
`mrecv`/`imrecv` report `MPI_ANY_TAG`; a UCX
receive that matches immediately raises `recv_complete` before `recv_start` (pair by token).

### 3.5 WP-B: collective algorithm event (branch `mpit/coll-events`)

One event, `ompi.coll_algorithm_selected`, bound to a communicator, level 4. Payload: request
(non-blocking collectives), msg_size (bytes per process), parent_comm, operation (`COLLTYPE`
number), algorithm (component specific id), component (1 basic, 2 tuned, 3 han, 4 libnbc, ...),
comm_size, root, level (0 user communicator, 1 inter-node, 2 intra-node helper of HAN).
For `tuned` and `libnbc` the algorithm id is the value of the control variable
`coll_<component>_<operation>_algorithm`, so the name comes from that variable's enumeration;
no new enumeration was needed.

Implementation: `ompi/mca/coll/base/coll_base_events.[ch]`; raised in the 14 `tuned`
`*_intra_do_this` functions (every fixed and dynamic decision ends there), in `libnbc` for
`ibcast`, `iallreduce`, `ireduce`, `iallgather` (with the request token), and at the dynamic
dispatch of `han`. HAN registers its helper communicators; their events are delivered to
registrations on the **user** communicator (a tool cannot reach the helper communicators), with
`level` 1/2 and `parent_comm`.

| Test (job 10379, `cluster/test_coll_events.sbatch`, 2 nodes x 2 ranks, new test `mpi_t_event_coll_algorithm`) | Result |
|---|---|
| `coll ^han`, `pml ob1` and `pml ucx`: bcast, allreduce, barrier events with right operation, communicator size, message size | PASS |
| forced `tuned` bcast algorithm 6 and 3 (`coll_tuned_use_dynamic_rules 1`): reported id equals the forced id | PASS |
| default (HAN active): HAN decision at level 0; tuned and libnbc algorithms on the helper communicators at level 1 and 2 with `parent_comm` | observed in the event log; the test checks the level-0 events |
| singleton run | skips (needs 2 processes) |

Bugs found while testing: HAN events were first bound to the helper communicators, where no tool
can register; they are now delivered to the user communicator. The test first matched the
internal allreduce of communicator creation; it now matches by message size.

Other components (commit after `bc44b7a1b5` on the same branch): `basic` (thin wrappers around
the linear base algorithms; ids 1 linear, 2 logarithmic, 3 inter-communicator), `adapt`
(`ibcast`/`ireduce`, tree type as id, with request), `xhc`, `acoll`, `ucc` (id 1: the algorithm
choice happens inside the component). `coll/sm` does not exist in this Open MPI version.

| Test (job 10385/10392, `cluster/test_coll_events.sbatch`) | Result |
|---|---|
| `coll basic,libnbc,self`: bcast/allreduce/barrier events from component `basic` | PASS |
| `coll_adapt_priority 100`: bcast event from component `adapt` | PASS |
| `coll_xhc_priority 100` (one node, 2 ranks): events from `xhc` | PASS |
| `coll_acoll_priority 100 --mca coll_acoll_comm_size_thresh 2` (acoll is off below 16 ranks): events from `acoll` | PASS |
| `ucc`: no UCC runtime here, so only a syntax check of all `coll/ucc` sources against the UCC 1.7.0 headers (`cluster/ucc_syntax.sbatch`, job 10390) | no errors or warnings; **never run** |

Regression after WP-A and WP-B (dev debug build): `ompi/test/t` 24 PASS, 1 SKIP (the new
2-rank collective test in its singleton run); `test/util` 33/33 PASS (job 10393).
`mpi_t_stress` (10000 MPI_T init/finalize cycles) is very slow under `make check` on the BeeGFS
build tree (state D, waiting for I/O; one run was cancelled after 30 min) but passes when run
directly (2 min 15 s, job 10383); the per-cycle cost is the same as for unmodified upstream
(13.8 vs 13.2 ms per cycle, job 10382, `tests/mpit_cycles.c`). A build tree on node-local disk
would avoid this; it was not repeated.

Known limits: `libnbc` reports only four operations and, for persistent collectives, at
initialisation instead of every start; `han` ids are 0 (default task based) and 255 (default
simple) when no algorithm table entry is selected; `xhc`, `acoll`, `ucc` report no sub-algorithm;
`ucc` non-blocking operations and `adapt`/`libnbc` persistent variants are not covered.

### 3.6 Tool milestone G1: store, monitor thread, file sink, `MPI_Init`/`MPI_Finalize` wrappers

Code in `src/core/` (`record.h` 128-byte record, `store.[ch]` lock-free segment store and monitor
thread, `file_sink.c`) and `src/pmpi/init.c`; format in `docs/LOG_FORMAT.md`, reader
`tools/edumpi_dump.py`. A record is claimed with one atomic add and written into by pointer, also
later by another thread; segments of 2048 records are written and unmapped by the monitor thread
once all their records are complete (default every 500 ms, `EDUMPI_FLUSH_MS`); an open record
keeps only its own segment in memory. The tool is injected with `LD_PRELOAD=libedumpi.so`.

| Test | Result |
|---|---|
| `tests/test_store.c`: 8 threads, 8,000,000 records on macOS (2.4 million flushed while running), every 1000th record kept open and completed later by another thread | all records exactly once, content intact, 0 dropped |
| same test on the cluster (4,000,000 records, job 10398) | PASS |
| `tests/test_init.c` (plain MPI program, tool via `LD_PRELOAD`), 2 nodes x 2 ranks, Open MPI `dev-dbg` | 4 files with correct header (rank, world size, node) and trailer |

Not in G1: PostgreSQL binary COPY (planned as G1b, the old `pgcopy.c` is in `legacy/`), wrapped MPI
calls (G2), MPI_T (G3 onwards). Segments are `munmap`ed after writing; recycling them (as the
`eduhpx_mpi_data` branch does to avoid page-fault cost) is not done yet and matters for the
overhead measurement.

### 3.7 Tool milestone G2: PMPI wrappers generated from one table

`tools/gen_pmpi.py` holds a table of 70 MPI calls (blocking and non-blocking point-to-point,
send-receive incl. MPI 4 `Isendrecv`, blocking and non-blocking collectives incl. neighbour
collectives, one-sided incl. request-based) with, per call, the expressions for bytes sent,
bytes received, peer and communicator; it generates `src/pmpi/wrappers.c`. By hand in
`src/pmpi/track.c`: caches of communicator and window (label, rank, size, world-rank translation,
dropped at `MPI_Comm_free` / `MPI_Win_free`), the request table, and the wrappers of `Wait`
(recorded as its own call, as in the fork), `Waitall/any/some`, `Test*`, `Request_free`.
A non-blocking call leaves its record open; the wait or test that observes completion fills
`done_ns` (and, for a receive, the real sender and the received bytes) and completes it.

| Test (job 10399, `tests/test_calls.c` with known traffic on 4 ranks over 2 nodes, `tests/check_calls.py`) | Result |
|---|---|
| `Send/Recv` ring 1000 B, `Irecv(ANY_SOURCE)/Isend` 800 B with `Waitall`, `Sendrecv` 64 B, `Bcast` 400 B (root and others), `Allreduce` 80 B on world and on a split communicator, `Gather`, `Alltoallv`, communicator labels for world/split/dup, `Ibcast` + `Wait`, `Put` 40 B | 60 of 60 checks PASS (15 per rank): sizes, world-rank partners, real sender of an any-source receive, completion after start, no open record at the end |

Limits of this PMPI-only layer (by design; the MPI_T backend replaces them): the completion time of a
non-blocking call is when the wait or test returned, not when the message arrived; collective
byte counts are computed from the arguments, not from the library's internal messages; no
algorithm, no peer bitmap, no late sender/receiver; neighbour collectives count one neighbour;
persistent requests (`*_init`, `Start`) and the `_c` large-count variants are not wrapped;
`MPI_Wait*`/`Test*` of persistent or generalized requests are not tracked.

### 3.8 Tool milestone G3: MPI_T backend

`src/mpit/mpit.c`: before `PMPI_Init` the tool calls `MPI_T_init_thread`, looks up the events by
name (`ompi.pml_send_start`, `_recv_start`, `_send_complete`, `_recv_complete`,
`ompi.coll_algorithm_selected`, `ompi.mpi.communicator_created/_freed`), checks their element
counts and byte offsets, and registers the five communicator-bound events on `MPI_COMM_WORLD`,
`MPI_COMM_SELF` and, from the `communicator_created` callback, on every new communicator (this
includes the helper communicators HAN creates). A missing event (stock Open MPI) switches the
tool to PMPI only (`EDUMPI_MPIT=0` does the same by hand). Callbacks make no MPI calls and read
the payload with `MPI_T_event_copy`. The clock of the event source is mapped to `CLOCK_REALTIME`
once at start (best of 7 back-to-back samples); drift is not corrected yet.

What the events add to the records (flags `MPIT_SUM`, `EVENT_DONE`, field `coll_alg`):

| Data | Source | Replaces |
|---|---|---|
| arrival time, real sender and received bytes of `Irecv`/`Recv` | `pml_recv_complete` of a user message, found through the request token (= the `MPI_Request` handle; an event that arrives before the call returned is stashed and applied when the request is entered) | time of the wait or test |
| collective `send_bytes`/`recv_bytes` | sum of the library's internal messages (negative tag) seen by this thread during the blocking call | bytes computed from the arguments |
| collective partner bitmap | world ranks from `pml_send_start` with a negative tag | not available |
| collective algorithm (`coll_alg[0..2]`: user, inter-node, intra-node communicator) | `coll_algorithm_selected`, level 0/1/2 | not available |

| Test (jobs 10404, 10408; `tests/test_calls.c`, 4 ranks over 2 nodes; checker with `EXPECT_MPIT=1`) | Result |
|---|---|
| `pml ob1`, `pml ucx`, `pml ob1 --mca coll ^han`: Irecv and Recv completion from the event with sender and bytes; Allreduce and Alltoallv have internal byte sums and non-empty partner bitmaps; every collective has an algorithm (HAN: `3.0`, inter-node `2.3`/`4.1`, intra-node `2.3`/`2.5`; without HAN `2.x`) | PASS |
| `EDUMPI_MPIT=0` (PMPI only) | PASS (records as in G2) |

Bugs found on the way: `communicator_created` carries the context id first and the handle second
(the first version read the wrong element and registered bogus handles: 4 of 19 communicators
instead of all); the UCX persistent requests were the last PML path without the common events
(closed in the fork, branch `mpit/pml-common-events`, test extended). Not done: non-blocking
collectives get their algorithm only if the event is raised inside the call (libnbc does), no
byte sums (the schedule runs later); late sender/receiver (G4); PostgreSQL sink (G1b).

### 3.9 Tool milestone G4: late sender and late receiver

With `pml ob1` the tool also registers `ompi.pml_ob1_request_activate`, `_posted_match` and
`_transfer` (optional; absent with `pml ucx`). During a user point-to-point call the activation
event stores token, record and event time; `posted_match` of the same token gives
`late_sender_ns` (the receiver waited from the activation until a message matched), the first
`transfer` gives `late_receiver_ns` (rendezvous: the sender waited from the activation until the
receiver took part; an eager send has no `transfer` event and therefore 0). Entries are dropped at
the completion event and when a recycled request structure is activated again. Rules from the
WP-0 measurements (section 19 of the plan).

| Test (job 10409, `tests/test_late.c` with 300 ms delays, `pml ob1`, tcp, 2 nodes) | Result |
|---|---|
| A: sender sleeps 0.3 s, receiver blocks in `MPI_Recv` | late sender 300.2 ms; no late receiver for the eager message |
| B: 4 MiB rendezvous, receiver sleeps 0.3 s | late receiver 300.4 ms at the sender; late sender 0.2 ms |
| C: 8 B eager to a late receiver | late receiver 0.0 ms |

Limits: ob1 only (as in the fork: with UCX the matching is inside UCX; WP-U); persistent
requests and non-blocking calls whose `Irecv` returns before the match are handled through the
token, not tested separately; receives that match in the unexpected queue (message first) have
late sender 0 by definition.

### 3.10 Tool milestone G1b: PostgreSQL sink, algorithm names

`src/core/pg_sink.c` (build with `make PG=1 PQINC=... PQLIB=...`) streams records by binary `COPY`
into the **unchanged** table `edumpi_running_data` of EduStore: same environment variables
(`DB_HOST`, `DB_PORT`, `DB_USER`, `DB_NAME`, `DB_PW`, run id `SLURM_JOBID` or `EDUMPI_RUN_ID`,
`EDUMPI_PROGRAM`), rank 0 inserts the run, every rank its `edumpi_cluster_info` row, rank 0 sets
`end_time` at the end. Row semantics as in the fork: `time_start` rounded to the second, `time_end`
and `time_diff` exact, `MPI_Irecv` ends at the arrival, a send still in flight gets an extra
`MPI_Isend_done` row, `coll_partnerranks` is a 50-byte bitmap (longer for more than 400 ranks)
and empty for point-to-point. `EDUMPI_SINK=file|pg|both` (default `pg` when `DB_HOST` is set).
Algorithm names come from the enumerations of `coll_tuned_<op>_algorithm` and
`coll_libnbc_i<op>_algorithm` read through MPI_T at start (`tuned recursive_doubling`,
`libnbc linear`, `basic log`, `HAN(inter: libnbc linear, intra: tuned knomial)`).

| Test (job 10414, `cluster/tool_pg_check.sbatch`: private PostgreSQL 17 server on the node, listening on 127.0.0.1 only; 4 ranks; `tests/sql/check_calls.sql`) | Result |
|---|---|
| one run row with end time, four cluster rows; `MPI_Send`/`Recv` sizes and partners; `MPI_Irecv` row ends at arrival; 8 collective `Allreduce` rows with algorithm text; 50-byte bitmaps; `MPI_Put` as one-sided; `time_start` whole seconds; no row ends before it starts; processor name and rank filled | 11 of 11 PASS |

Not tested: TimescaleDB itself (the test uses the plain table definitions; the GUI and the
continuous aggregates were not run against rows of the new writer), several nodes writing at once
to one server, very large runs (batching is per monitor flush, 0.5 s).

### 3.4 Not done yet

Measured overhead of the new events (WP-H), PVARs/CVARs
(WP-D/F), request lifetime events (WP-C), the measurement tool itself (WP-G), `btl uct` for ob1,
cross-node clock offset. The `make check` run of the whole fork after WP-A was not repeated;
only the tests listed above ran with the WP-A changes.

## 4. Reproducing

```bash
# on the cluster, sources in ~/edumpi_t/src/code-ompi_edumpi_t
sbatch cluster/dev_build.sbatch dev          # incremental debug build -> install/dev-dbg
sbatch cluster/test_pml_events.sbatch dev    # builds and runs mpi_t_event_pml_p2p and the probe
sbatch cluster/util_check.sbatch dev         # test/util check
sbatch cluster/test_coll_events.sbatch dev   # WP-B test in several modes (2 nodes x 2 ranks)
```

The probe `tests/mpit_ob1_probe.c` registers every event whose name starts with a prefix
(default `ompi.pml_ob1_`; `ompi.pml_` for the new ones) and prints one line per event.
