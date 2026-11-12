#!/usr/bin/env python3
"""usage: check_nbc.py <dir> <ranks>   (files of tests/test_nbc.c, run with the MPI_T backend)"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from edumpi_dump import read_file  # noqa: E402

d, n = sys.argv[1], int(sys.argv[2])
SUM, EVT, HAS_DONE = 128, 256, 8
fails = 0


def check(what, cond, detail=''):
    global fails
    print(('PASS: ' if cond else 'FAIL: ') + what + (' ' + detail if detail and not cond else ''))
    fails += 0 if cond else 1


recs = {}
for r in range(n):
    _, rr = read_file(os.path.join(d, 'edumpi.%d.bin' % r))
    recs[r] = rr
tot_sent = tot_recv = 0
for r in range(n):
    ia = [x for x in recs[r] if x['function'] == 'MPI_Iallreduce']
    a0, a1, b = ia[0], ia[1], ia[2]
    bits = lambda x: sum(bin(w).count('1') for w in x['partner_bits'])
    t = 'rank %d: ' % r
    check(t + 'A alone has byte sums from internal messages', a0['flags'] & SUM and a0['send_bytes'] > 0 and a0['recv_bytes'] > 0 and bits(a0) > 0,
          'flags=%s send=%d recv=%d' % (a0['flagnames'], a0['send_bytes'], a0['recv_bytes']))
    check(t + 'A overlapped with B, a blocking Allreduce and a Sendrecv has the same sums and partners as A alone',
          (a1['send_bytes'], a1['recv_bytes'], a1['partner_bits']) == (a0['send_bytes'], a0['recv_bytes'], a0['partner_bits']),
          'alone send=%d recv=%d, overlapped send=%d recv=%d' % (a0['send_bytes'], a0['recv_bytes'], a1['send_bytes'], a1['recv_bytes']))
    check(t + 'B (800 B per rank) moves more than A (80 B)', b['send_bytes'] > a1['send_bytes'] and b['flags'] & SUM,
          'A=%d B=%d' % (a1['send_bytes'], b['send_bytes']))
    check(t + 'completion time of A, B comes from the request end event and not after the wait',
          all(x['flags'] & EVT and x['flags'] & HAS_DONE and x['done_ns'] >= x['start_ns'] for x in (a0, a1, b)),
          'flags A0=%s A1=%s B=%s' % (a0['flagnames'], a1['flagnames'], b['flagnames']))
    w = [x for x in recs[r] if x['function'] in ('MPI_Wait', 'MPI_Waitall')]
    # the end event is stamped with the library's clock and mapped to the tool's clock with an offset measured at start:
    # the two agree within some microseconds, so allow 100 us
    check(t + 'the request ended before the wait returned (within the clock mapping error)', a0['done_ns'] <= w[0]['end_ns'] + 100000)
    tot_sent += a1['send_bytes']
    tot_recv += a1['recv_bytes']
check('A overlapped: total bytes sent by all ranks equal total bytes received', tot_sent == tot_recv, 'sent=%d recv=%d' % (tot_sent, tot_recv))
print('RESULT: %s (%d failures)' % ('PASS' if fails == 0 else 'FAIL', fails))
sys.exit(1 if fails else 0)
