#!/usr/bin/env python3
"""usage: check_persist.py <dir> <ranks> [mpit]   (files of tests/test_persist.c)"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from edumpi_dump import read_file  # noqa: E402

d, n = sys.argv[1], int(sys.argv[2])
mpit = len(sys.argv) > 3
PERSIST, HAS_DONE, COLL, SUM, EVT = 512, 8, 2, 128, 256
fails = 0


def check(what, cond, detail=''):
    global fails
    print(('PASS: ' if cond else 'FAIL: ') + what + (' ' + detail if detail and not cond else ''))
    fails += 0 if cond else 1


for r in range(n):
    _, recs = read_file(os.path.join(d, 'edumpi.%d.bin' % r))
    t = 'rank %d: ' % r
    f = lambda name: [x for x in recs if x['function'] == name]
    check(t + 'init calls recorded', len(f('MPI_Recv_init')) == 1 and len(f('MPI_Send_init')) == 1 and len(f('MPI_Allreduce_init')) == 1)
    starts = [x for x in f('MPI_Start') if x['flags'] & PERSIST]
    p2p = [x for x in starts if not x['flags'] & COLL]
    coll = [x for x in starts if x['flags'] & COLL]
    check(t + '10 p2p activations (5 sends, 5 receives) and 3 collective activations', len(p2p) == 10 and len(coll) == 3, 'p2p=%d coll=%d' % (len(p2p), len(coll)))
    sends = [x for x in p2p if x['send_bytes'] == 100]
    recvs = [x for x in p2p if x['recv_bytes'] == 100]
    check(t + 'sends carry 100 B and the next rank, receives 100 B and the previous rank',
          len(sends) == 5 and all(x['partner'] == (r + 1) % n for x in sends) and len(recvs) == 5 and all(x['partner'] == (r + n - 1) % n for x in recvs),
          'sends=%d recvs=%d' % (len(sends), len(recvs)))
    check(t + 'every activation was completed by the wait (done time)', all(x['flags'] & HAS_DONE and x['done_ns'] >= x['start_ns'] for x in starts))
    if mpit:
        check(t + 'collective activations: MPI_T sums and algorithm', all(x['flags'] & SUM and x['send_bytes'] > 0 and x['coll_alg'][0] != 0 for x in coll),
              ' '.join('%s/%d/%s' % (x['flagnames'], x['send_bytes'], x['coll_alg']) for x in coll))
        check(t + 'collective activations: completion from the request event', all(x['flags'] & EVT for x in coll))
print('RESULT: %s (%d failures)' % ('PASS' if fails == 0 else 'FAIL', fails))
sys.exit(1 if fails else 0)
