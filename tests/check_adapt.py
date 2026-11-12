#!/usr/bin/env python3
"""usage: check_adapt.py <dir> <ranks>   (files of tests/test_adapt.c, coll/adapt, MPI_T backend)"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from edumpi_dump import read_file  # noqa: E402

d, n = sys.argv[1], int(sys.argv[2])
SUM, EVT = 128, 256
fails = 0


def check(what, cond, detail=''):
    global fails
    print(('PASS: ' if cond else 'FAIL: ') + what + (' ' + detail if detail and not cond else ''))
    fails += 0 if cond else 1


recs = {r: read_file(os.path.join(d, 'edumpi.%d.bin' % r))[1] for r in range(n)}
for fn in ('MPI_Ibcast', 'MPI_Ireduce'):
    ts = tr = 0
    for r in range(n):
        x = [y for y in recs[r] if y['function'] == fn]
        a0, a1, b = x[0], x[1], x[2]
        t = '%s rank %d: ' % (fn, r)
        check(t + 'A alone and A overlapped have the same sums and partners',
              (a0['send_bytes'], a0['recv_bytes'], a0['partner_bits']) == (a1['send_bytes'], a1['recv_bytes'], a1['partner_bits']),
              'alone %d/%d overlapped %d/%d' % (a0['send_bytes'], a0['recv_bytes'], a1['send_bytes'], a1['recv_bytes']))
        check(t + 'sums come from the internal messages', a0['flags'] & SUM and (a0['send_bytes'] + a0['recv_bytes']) > 0,
              'flags=%s %d/%d' % (a0['flagnames'], a0['send_bytes'], a0['recv_bytes']))
        check(t + 'B moves ten times the bytes of A', (b['send_bytes'] + b['recv_bytes']) == 10 * (a1['send_bytes'] + a1['recv_bytes']),
              'A=%d B=%d' % (a1['send_bytes'] + a1['recv_bytes'], b['send_bytes'] + b['recv_bytes']))
        check(t + 'request end event used for the completion time', all(y['flags'] & EVT for y in (a0, a1, b)),
              '%s %s %s' % (a0['flagnames'], a1['flagnames'], b['flagnames']))
        ts += a1['send_bytes']
        tr += a1['recv_bytes']
    check(fn + ': total bytes sent equal total bytes received', ts == tr, 'sent=%d recv=%d' % (ts, tr))
print('RESULT: %s (%d failures)' % ('PASS' if fails == 0 else 'FAIL', fails))
sys.exit(1 if fails else 0)
