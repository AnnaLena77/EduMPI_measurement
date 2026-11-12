# Status: what is done and what was tested

Last updated 2026-11-12. This page records only what was actually built and run. The design
and the full plan are in `EDUMPI_T_DEVELOPMENT_PLAN.md` (kept next to this repository).

## 1. Overview

| Part | State |
|---|---|
| Open MPI fork `FuldaHPC/code-ompi_edumpi_t` (base: upstream `main`, 6.1.0a1, commit `89b41f785f`) | builds (release and debug); WP-A on branch `mpit/pml-common-events` and WP-B on branch `mpit/coll-events` (stacked on WP-A), both implemented, pushed to the fork and tested |
| This tool (`EduMPI_measurement`) | not started: skeleton, `legacy/`, probe, cluster job scripts |
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
`mrecv`/`imrecv` report `MPI_ANY_TAG`; persistent UCX requests are not instrumented; a UCX
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

Known limits: `libnbc` reports only four operations and, for persistent collectives, at
initialisation instead of every start; `han` ids are 0 (default task based) and 255 (default
simple) when no algorithm table entry is selected; `basic`, `adapt`, `xhc`, `acoll`, `ucc`, `sm`
are not instrumented; the new test is not part of `make check` runs with one process (it skips).
The whole-tree `make check` was not repeated after WP-B.

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
