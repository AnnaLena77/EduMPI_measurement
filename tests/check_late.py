#!/usr/bin/env python3
"""usage: check_late.py <dir>   (files of tests/test_late.c)"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from edumpi_dump import read_file  # noqa: E402

d = sys.argv[1]
fails = 0


def check(what, cond, detail=''):
    global fails
    print(('PASS: ' if cond else 'FAIL: ') + what + (' ' + detail if detail and not cond else ''))
    fails += 0 if cond else 1


_, r0 = read_file(os.path.join(d, 'edumpi.0.bin'))
_, r1 = read_file(os.path.join(d, 'edumpi.1.bin'))
sends = [r for r in r0 if r['function'] == 'MPI_Send']
recvs = [r for r in r1 if r['function'] == 'MPI_Recv']
ms = lambda ns: ns / 1e6
a, b, c = recvs[0], sends[1], sends[2]
check('A: late sender at the receiver is about 300 ms (%.1f ms)' % ms(a['late_sender_ns']), 250e6 < a['late_sender_ns'] < 500e6)
check('A: no late receiver for the eager send', sends[0]['late_receiver_ns'] == 0)
check('B: late receiver at the 4 MiB sender is about 300 ms (%.1f ms)' % ms(b['late_receiver_ns']), 250e6 < b['late_receiver_ns'] < 600e6)
check('B: the receive of 4 MiB was posted late, so no late sender (%.1f ms)' % ms(recvs[1]['late_sender_ns']), recvs[1]['late_sender_ns'] < 50e6)
check('C: eager send to a late receiver has no late-receiver time (%.1f ms)' % ms(c['late_receiver_ns']), c['late_receiver_ns'] == 0)
print('RESULT: %s (%d failures)' % ('PASS' if fails == 0 else 'FAIL', fails))
sys.exit(1 if fails else 0)
