#!/usr/bin/env python3
"""Print an edumpi.<rank>.bin file (format 1) as CSV. Standard library only. See docs/LOG_FORMAT.md.
As a module: read_file(path) -> (header dict, records list of dicts)."""
import struct
import sys

REC = struct.Struct('<QQQqqiHHIIIII3IQQQIIQQ')   # 128 bytes, see record.h
FLAGS = ['BLOCKING', 'COLLECTIVE', 'ONESIDED', 'HAS_DONE', 'ANY_SOURCE', 'NOT_COMPLETED', 'NO_END', 'MPIT_SUM', 'EVENT_DONE']


def read_file(path):
    d = open(path, 'rb').read()
    if d[:8] != b'EDUMPIT1':
        raise ValueError('not an EDUMPIT1 file')
    ver, recsize, rank, size = struct.unpack_from('<IIii', d, 8)
    node = d[24:56].split(b'\0')[0].decode()
    trailer = struct.unpack_from('<Q', d, 56)[0]
    end = trailer if trailer else len(d)
    funcs, comms = [], []
    if trailer:
        nf, nc = struct.unpack_from('<II', d, trailer + 8)
        o = trailer + 16
        funcs = [d[o + 32 * i:o + 32 * i + 32].split(b'\0')[0].decode() for i in range(nf)]
        o += 32 * nf
        comms = [d[o + 64 * i:o + 64 * i + 64].split(b'\0')[0].decode() for i in range(nc)]
    hdr = dict(rank=rank, world_size=size, node=node, version=ver, trailer=bool(trailer), funcs=funcs, comms=comms)
    recs = []
    o = 64
    while o + recsize <= end:
        (start, endn, done, sb, rb, partner, comm, func, flags, seq, _state, sc, rc, a0, a1, a2,
         ls, lr, _pb, nwords, _p2, _tok, _p3) = REC.unpack_from(d, o)
        o += recsize
        bits = ()
        if nwords:
            bits = struct.unpack_from('<%dQ' % nwords, d, o)
            o += 8 * nwords
        recs.append(dict(seq=seq, function=funcs[func] if func < len(funcs) else func,
                         comm=comms[comm] if comm < len(comms) else comm, start_ns=start, end_ns=endn,
                         done_ns=done, send_bytes=sb, recv_bytes=rb, partner=partner, flags=flags,
                         flagnames='|'.join(n for i, n in enumerate(FLAGS) if flags & (1 << i)),
                         coll_alg=(a0, a1, a2), coll_op=_p2, late_sender_ns=ls, late_receiver_ns=lr, partner_bits=bits))
    return hdr, recs


def main(path):
    h, recs = read_file(path)
    print('# rank=%d world_size=%d node=%s version=%d trailer=%s' % (h['rank'], h['world_size'], h['node'], h['version'], h['trailer']))
    print('seq,function,comm,start_ns,end_ns,done_ns,send_bytes,recv_bytes,partner,flags,coll_alg,late_sender_ns,late_receiver_ns,partner_bits')
    for r in recs:
        print(','.join(map(str, [r['seq'], r['function'], r['comm'], r['start_ns'], r['end_ns'], r['done_ns'], r['send_bytes'],
                                 r['recv_bytes'], r['partner'], r['flagnames'], '%d/%d/%d' % r['coll_alg'],
                                 r['late_sender_ns'], r['late_receiver_ns'], ''.join('%016x' % w for w in r['partner_bits'])])))


if __name__ == '__main__':
    main(sys.argv[1])
