#!/usr/bin/env python3
"""Print an edumpi.<rank>.bin file (format 1) as CSV. Standard library only. See docs/LOG_FORMAT.md."""
import struct
import sys

REC = struct.Struct('<QQQqqiHHIIIII3IQQQIIQQ')   # 128 bytes, see record.h
FLAGS = ['BLOCKING', 'COLLECTIVE', 'ONESIDED', 'HAS_DONE', 'ANY_SOURCE', 'NOT_COMPLETED', 'NO_END']


def main(path):
    d = open(path, 'rb').read()
    if d[:8] != b'EDUMPIT1':
        sys.exit('not an EDUMPIT1 file')
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
    print('# rank=%d world_size=%d node=%s version=%d trailer=%s' % (rank, size, node, ver, bool(trailer)))
    print('seq,function,comm,start_ns,end_ns,done_ns,send_bytes,recv_bytes,partner,flags,coll_alg,late_sender_ns,late_receiver_ns,partner_bits')
    o = 64
    while o + recsize <= end:
        f = REC.unpack_from(d, o)
        (start, endn, done, sb, rb, partner, comm, func, flags, seq, _state, sc, rc, a0, a1, a2,
         ls, lr, _pb, nwords, _p2, _tok, _p3) = f
        o += recsize
        bits = ''
        if nwords:
            bits = ''.join('%016x' % w for w in struct.unpack_from('<%dQ' % nwords, d, o))
            o += 8 * nwords
        fl = '|'.join(n for i, n in enumerate(FLAGS) if flags & (1 << i))
        print(','.join(map(str, [seq, funcs[func] if func < len(funcs) else func,
                                 comms[comm] if comm < len(comms) else comm, start, endn, done, sb, rb, partner,
                                 fl, '%d/%d/%d' % (a0, a1, a2), ls, lr, bits])))


if __name__ == '__main__':
    main(sys.argv[1])
