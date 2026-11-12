#!/usr/bin/env python3
"""G5: compare the row summaries of the original fork (rows_old.txt) and of the new tool (rows_new.txt).
Line format: rank|function|type|area|send|recv|partner|algorithm|bitmap|count (times are not compared).
Prints, per (rank, function), the fields that differ; the algorithm text is not compared (the two builds are different
Open MPI versions, so decisions and names differ) and the partner of collective rows is not compared (the fork
stores one internal peer there, the tool the bitmap only)."""
import collections, sys

def load(p):
    d = collections.defaultdict(list)
    for l in open(p):
        r = l.rstrip('\n').split('|')
        d[(int(r[0]), r[1])].append(r[2:])
    return d

o, n = load(sys.argv[1]), load(sys.argv[2])
bad = 0
for k in sorted(set(o) | set(n)):
    a, b = o.get(k, []), n.get(k, [])
    flag = []
    ca, cb = sum(int(x[-1]) for x in a), sum(int(x[-1]) for x in b)
    if ca != cb:
        flag.append("rows %d vs %d" % (ca, cb))
    for name, i in (("type", 0), ("send", 2), ("recv", 3), ("partner", 4), ("bitmap", 6)):
        if name == "partner" and any(x[0] == "collective" for x in a + b):
            continue
        sa, sb = sorted(set(x[i] for x in a)), sorted(set(x[i] for x in b))
        if sa != sb:
            flag.append("%s %s vs %s" % (name, [s[:14] for s in sa], [s[:14] for s in sb]))
    if flag:
        bad += 1
        print(k, "; ".join(flag))
print("groups with differences: %d of %d" % (bad, len(set(o) | set(n))))
