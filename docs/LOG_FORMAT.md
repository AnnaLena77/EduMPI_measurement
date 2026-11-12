# Log file format 1 (`edumpi.<rank>.bin`)

One file per rank in `$EDUMPI_LOG_DIR` (default `.`), written by the file sink. Little endian. All times are
`CLOCK_REALTIME` nanoseconds since 1970-01-01 UTC, `0` means "not set". Clocks of different nodes are not
synchronised. `tools/edumpi_dump.py <file>` prints the content as CSV.

```
header   64 bytes
records  n * (128 bytes + optional bitmap)
trailer  16 + nf * 32 + nc * 64 bytes
```

Header: magic `EDUMPIT1` (8), version = 1 (u32), record size = 128 (u32), rank (i32), world size (i32), node name (32,
zero terminated), trailer offset (u64; `0` while the file is open or after a crash: then read records to the end of the
file, names cannot be resolved).

Record (128 bytes, `src/core/record.h`):

| offset | size | field |
|---|---|---|
| 0 / 8 / 16 | 8 each | `start_ns`, `end_ns`, `done_ns` (valid with flag `HAS_DONE`) |
| 24 / 32 | 8 each | `send_bytes`, `recv_bytes` (i64) |
| 40 | 4 | `partner` (i32), world rank, -1 unknown |
| 44 / 46 | 2 each | `comm`, `func`: indexes into the trailer tables |
| 48 | 4 | `flags`: 0 BLOCKING, 1 COLLECTIVE, 2 ONESIDED, 3 HAS_DONE, 4 ANY_SOURCE, 5 NOT_COMPLETED, 6 NO_END, 7 MPIT_SUM (collective byte counts and partner bitmap are sums over the library's internal messages), 8 EVENT_DONE (completion data from MPI_T events instead of a wait or test) |
| 52 | 4 | `seq`, number of the call on this rank |
| 56 | 4 | `state` (internal, 2 = complete) |
| 64 / 68 | 4 each | `sendcount`, `recvcount` |
| 72 | 12 | `coll_alg[3]` (u32 each): algorithm id of the user, inter-node, intra-node communicator, `component << 16 \| id`, 0 none |
| 88 / 96 | 8 each | `late_sender_ns`, `late_receiver_ns` |
| 104 | 8 | pointer field, always 0 in the file |
| 112 | 4 | `partner_bits_words`: if not 0, that many u64 follow the record directly (bitmap of world ranks sent to inside a collective) |
| 120 | 8 | `request_token` while a request is open (0 in a finished record) |

Trailer: magic `EDUMPITR`, `nf` (u32), `nc` (u32), `nf` function names (32 bytes each), `nc` communicator labels (64 bytes each).
Records are in the order segments were written; sort by `start_ns` or use `seq`.
