#!/usr/bin/env python3
"""Check the files written for tests/test_calls.c.  usage: check_calls.py <dir> <ranks>"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from edumpi_dump import read_file  # noqa: E402

BLOCKING, COLL, OS, HAS_DONE, ANY = 1, 2, 4, 8, 16
d, n = sys.argv[1], int(sys.argv[2])
fails = 0


def check(what, cond):
    global fails
    print(('PASS: ' if cond else 'FAIL: ') + what)
    fails += 0 if cond else 1


for rank in range(n):
    h, recs = read_file(os.path.join(d, 'edumpi.%d.bin' % rank))
    nxt, prv = (rank + 1) % n, (rank + n - 1) % n
    by = {}
    for r in recs:
        by.setdefault(r['function'], []).append(r)
    t = 'rank %d: ' % rank
    check(t + 'header', h['rank'] == rank and h['world_size'] == n and h['trailer'])
    check(t + 'all records complete in time order', all(r['end_ns'] >= r['start_ns'] > 0 for r in recs))
    s = by['MPI_Send'][0]
    check(t + 'MPI_Send 1000 B to next', s['send_bytes'] == 1000 and s['partner'] == nxt and s['flags'] & BLOCKING)
    r = by['MPI_Recv'][0]
    check(t + 'MPI_Recv 1000 B from prev', r['recv_bytes'] == 1000 and r['partner'] == prv)
    ir = by['MPI_Irecv'][0]
    check(t + 'MPI_Irecv 800 B from prev (any source, real sender), done after start',
          ir['recv_bytes'] == 800 and ir['partner'] == prv and ir['flags'] & ANY and ir['flags'] & HAS_DONE and ir['done_ns'] >= ir['start_ns'])
    isd = by['MPI_Isend'][0]
    check(t + 'MPI_Isend 800 B to next with completion', isd['send_bytes'] == 800 and isd['partner'] == nxt and isd['flags'] & HAS_DONE)
    sr = by['MPI_Sendrecv'][0]
    check(t + 'MPI_Sendrecv 64 B each way', sr['send_bytes'] == 64 and sr['recv_bytes'] == 64 and sr['partner'] == prv)
    b = by['MPI_Bcast'][0]
    check(t + 'MPI_Bcast 400 B (%s)' % ('root sends' if rank == 1 else 'receives'),
          (b['send_bytes'], b['recv_bytes']) == ((400, 0) if rank == 1 else (0, 400)) and b['flags'] & COLL)
    ar = by['MPI_Allreduce']
    check(t + 'MPI_Allreduce 80 B on world and on the split communicator',
          len(ar) == 2 and all(a['send_bytes'] == 80 and a['recv_bytes'] == 80 for a in ar) and ar[0]['comm'] != ar[1]['comm'])
    g = by['MPI_Gather'][0]
    check(t + 'MPI_Gather 16 B sent%s' % (', %d B received at root' % (16 * n) if rank == 0 else ''),
          g['send_bytes'] == 16 and g['recv_bytes'] == (16 * n if rank == 0 else 0))
    a2 = by['MPI_Alltoallv'][0]
    check(t + 'MPI_Alltoallv %d B each way' % (4 * n), a2['send_bytes'] == 4 * n and a2['recv_bytes'] == 4 * n)
    check(t + 'communicator labels: world, split and dup are three different ones', len(set(r['comm'] for r in recs if r['comm'] != 0xFFFF)) >= 3)
    ib = by['MPI_Ibcast'][0]
    check(t + 'MPI_Ibcast completed by MPI_Wait', ib['flags'] & HAS_DONE and ib['done_ns'] >= ib['start_ns'] and 'MPI_Wait' in by)
    p = by['MPI_Put'][0]
    check(t + 'MPI_Put 40 B to next', p['send_bytes'] == 40 and p['partner'] == nxt and p['flags'] & OS)
    check(t + 'no record left open', not any(r['flags'] & 0x60 for r in recs))
print('RESULT: %s (%d failures)' % ('PASS' if fails == 0 else 'FAIL', fails))
sys.exit(1 if fails else 0)
