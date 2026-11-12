#!/usr/bin/env python3
"""Generate src/pmpi/wrappers.c: one PMPI wrapper per line of the table below.

Line format:  kind | name | parameters | attributes
kind:   send  blocking send           isend  non-blocking send
        recv  blocking receive        irecv  non-blocking receive
        sr    blocking send+receive   isr    non-blocking send+receive
        coll  blocking collective     icoll  non-blocking collective
        os    one-sided               ros    request-based one-sided
attributes (all C expressions over the parameters):
        s=  bytes sent        r=  bytes received (not for receives: taken from the status)
        peer=  rank of the partner in the communicator      src=  source rank of a receive
        comm=  communicator parameter (default comm)       win=  window parameter
        st=    status parameter (receives)                   v=4: needs MPI 4
Collective byte counts are computed from the arguments (the PMPI-only way); the MPI_T backend
replaces them by the sum over the library's internal messages. Neighbour collectives count one
neighbour only.
"""
import re
import sys

T = r'''
send|Send|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm|s=EB(count,datatype) peer=dest
send|Bsend|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm|s=EB(count,datatype) peer=dest
send|Ssend|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm|s=EB(count,datatype) peer=dest
send|Rsend|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm|s=EB(count,datatype) peer=dest
isend|Isend|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
isend|Ibsend|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
isend|Issend|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
isend|Irsend|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
pinit|Send_init|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
pinit|Bsend_init|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
pinit|Ssend_init|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
pinit|Rsend_init|const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest
pinit|Recv_init|void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request|src=source
recv|Recv|void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status|src=source st=status
irecv|Irecv|void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request|src=source
recv|Mrecv|void *buf, int count, MPI_Datatype datatype, MPI_Message *message, MPI_Status *status|st=status comm=MPI_COMM_NULL
irecv|Imrecv|void *buf, int count, MPI_Datatype datatype, MPI_Message *message, MPI_Request *request|comm=MPI_COMM_NULL
sr|Sendrecv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, int dest, int sendtag, void *recvbuf, int recvcount, MPI_Datatype recvtype, int source, int recvtag, MPI_Comm comm, MPI_Status *status|s=EB(sendcount,sendtype) peer=dest src=source st=status
sr|Sendrecv_replace|void *buf, int count, MPI_Datatype datatype, int dest, int sendtag, int source, int recvtag, MPI_Comm comm, MPI_Status *status|s=EB(count,datatype) peer=dest src=source st=status
isr|Isendrecv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, int dest, int sendtag, void *recvbuf, int recvcount, MPI_Datatype recvtype, int source, int recvtag, MPI_Comm comm, MPI_Request *request|s=EB(sendcount,sendtype) peer=dest src=source v=4
isr|Isendrecv_replace|void *buf, int count, MPI_Datatype datatype, int dest, int sendtag, int source, int recvtag, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) peer=dest src=source v=4
coll|Barrier|MPI_Comm comm|
coll|Bcast|void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm|s=(edumpi_crank(comm)==root?EB(count,datatype):0) r=(edumpi_crank(comm)!=root?EB(count,datatype):0)
coll|Allgather|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=EB(recvcount,recvtype)*edumpi_csize(comm)
coll|Allgatherv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int displs[], MPI_Datatype recvtype, MPI_Comm comm|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=edumpi_vsum(recvcounts,edumpi_csize(comm),recvtype)
coll|Allreduce|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm|s=EB(count,datatype) r=EB(count,datatype)
coll|Alltoall|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm|s=EB(recvcount,recvtype)*edumpi_csize(comm) r=EB(recvcount,recvtype)*edumpi_csize(comm)
coll|Alltoallv|const void *sendbuf, const int sendcounts[], const int sdispls[], MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int rdispls[], MPI_Datatype recvtype, MPI_Comm comm|s=edumpi_vsum(sendcounts,edumpi_csize(comm),sendtype) r=edumpi_vsum(recvcounts,edumpi_csize(comm),recvtype)
coll|Alltoallw|const void *sendbuf, const int sendcounts[], const int sdispls[], const MPI_Datatype sendtypes[], void *recvbuf, const int recvcounts[], const int rdispls[], const MPI_Datatype recvtypes[], MPI_Comm comm|s=edumpi_wsum(sendcounts,sendtypes,edumpi_csize(comm)) r=edumpi_wsum(recvcounts,recvtypes,edumpi_csize(comm))
coll|Exscan|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm|s=EB(count,datatype) r=EB(count,datatype)
coll|Scan|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm|s=EB(count,datatype) r=EB(count,datatype)
coll|Gather|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=(edumpi_crank(comm)==root?EB(recvcount,recvtype)*edumpi_csize(comm):0)
coll|Gatherv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int displs[], MPI_Datatype recvtype, int root, MPI_Comm comm|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=(edumpi_crank(comm)==root?edumpi_vsum(recvcounts,edumpi_csize(comm),recvtype):0)
coll|Reduce|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm|s=EB(count,datatype) r=(edumpi_crank(comm)==root?EB(count,datatype):0)
coll|Reduce_scatter|const void *sendbuf, void *recvbuf, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm|s=edumpi_vsum(recvcounts,edumpi_csize(comm),datatype) r=EB(recvcounts[edumpi_crank(comm)],datatype)
coll|Reduce_scatter_block|const void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm|s=EB(recvcount,datatype)*edumpi_csize(comm) r=EB(recvcount,datatype)
coll|Scatter|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm|s=(edumpi_crank(comm)==root?EB(sendcount,sendtype)*edumpi_csize(comm):0) r=(recvbuf==MPI_IN_PLACE?0:EB(recvcount,recvtype))
coll|Scatterv|const void *sendbuf, const int sendcounts[], const int displs[], MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm|s=(edumpi_crank(comm)==root?edumpi_vsum(sendcounts,edumpi_csize(comm),sendtype):0) r=(recvbuf==MPI_IN_PLACE?0:EB(recvcount,recvtype))
coll|Neighbor_allgather|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm|s=EB(sendcount,sendtype) r=EB(recvcount,recvtype)
coll|Neighbor_allgatherv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int displs[], MPI_Datatype recvtype, MPI_Comm comm|s=EB(sendcount,sendtype)
coll|Neighbor_alltoall|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm|s=EB(sendcount,sendtype) r=EB(recvcount,recvtype)
coll|Neighbor_alltoallv|const void *sendbuf, const int sendcounts[], const int sdispls[], MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int rdispls[], MPI_Datatype recvtype, MPI_Comm comm|
coll|Neighbor_alltoallw|const void *sendbuf, const int sendcounts[], const MPI_Aint sdispls[], const MPI_Datatype sendtypes[], void *recvbuf, const int recvcounts[], const MPI_Aint rdispls[], const MPI_Datatype recvtypes[], MPI_Comm comm|
icoll|Ibarrier|MPI_Comm comm, MPI_Request *request|
icoll|Ibcast|void *buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm, MPI_Request *request|s=(edumpi_crank(comm)==root?EB(count,datatype):0) r=(edumpi_crank(comm)!=root?EB(count,datatype):0)
icoll|Iallgather|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=EB(recvcount,recvtype)*edumpi_csize(comm)
icoll|Iallgatherv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int displs[], MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=edumpi_vsum(recvcounts,edumpi_csize(comm),recvtype)
icoll|Iallreduce|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) r=EB(count,datatype)
icoll|Ialltoall|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|s=EB(recvcount,recvtype)*edumpi_csize(comm) r=EB(recvcount,recvtype)*edumpi_csize(comm)
icoll|Ialltoallv|const void *sendbuf, const int sendcounts[], const int sdispls[], MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int rdispls[], MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|s=edumpi_vsum(sendcounts,edumpi_csize(comm),sendtype) r=edumpi_vsum(recvcounts,edumpi_csize(comm),recvtype)
icoll|Ialltoallw|const void *sendbuf, const int sendcounts[], const int sdispls[], const MPI_Datatype sendtypes[], void *recvbuf, const int recvcounts[], const int rdispls[], const MPI_Datatype recvtypes[], MPI_Comm comm, MPI_Request *request|s=edumpi_wsum(sendcounts,sendtypes,edumpi_csize(comm)) r=edumpi_wsum(recvcounts,recvtypes,edumpi_csize(comm))
icoll|Iexscan|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) r=EB(count,datatype)
icoll|Iscan|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) r=EB(count,datatype)
icoll|Igather|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Request *request|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=(edumpi_crank(comm)==root?EB(recvcount,recvtype)*edumpi_csize(comm):0)
icoll|Igatherv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int displs[], MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Request *request|s=(sendbuf==MPI_IN_PLACE?0:EB(sendcount,sendtype)) r=(edumpi_crank(comm)==root?edumpi_vsum(recvcounts,edumpi_csize(comm),recvtype):0)
icoll|Ireduce|const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm, MPI_Request *request|s=EB(count,datatype) r=(edumpi_crank(comm)==root?EB(count,datatype):0)
icoll|Ireduce_scatter|const void *sendbuf, void *recvbuf, const int recvcounts[], MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request|s=edumpi_vsum(recvcounts,edumpi_csize(comm),datatype) r=EB(recvcounts[edumpi_crank(comm)],datatype)
icoll|Ireduce_scatter_block|const void *sendbuf, void *recvbuf, int recvcount, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, MPI_Request *request|s=EB(recvcount,datatype)*edumpi_csize(comm) r=EB(recvcount,datatype)
icoll|Iscatter|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Request *request|s=(edumpi_crank(comm)==root?EB(sendcount,sendtype)*edumpi_csize(comm):0) r=(recvbuf==MPI_IN_PLACE?0:EB(recvcount,recvtype))
icoll|Iscatterv|const void *sendbuf, const int sendcounts[], const int displs[], MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm, MPI_Request *request|s=(edumpi_crank(comm)==root?edumpi_vsum(sendcounts,edumpi_csize(comm),sendtype):0) r=(recvbuf==MPI_IN_PLACE?0:EB(recvcount,recvtype))
icoll|Ineighbor_allgather|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|s=EB(sendcount,sendtype) r=EB(recvcount,recvtype)
icoll|Ineighbor_allgatherv|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int displs[], MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|s=EB(sendcount,sendtype)
icoll|Ineighbor_alltoall|const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|s=EB(sendcount,sendtype) r=EB(recvcount,recvtype)
icoll|Ineighbor_alltoallv|const void *sendbuf, const int sendcounts[], const int sdispls[], MPI_Datatype sendtype, void *recvbuf, const int recvcounts[], const int rdispls[], MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request|
icoll|Ineighbor_alltoallw|const void *sendbuf, const int sendcounts[], const MPI_Aint sdispls[], const MPI_Datatype sendtypes[], void *recvbuf, const int recvcounts[], const MPI_Aint rdispls[], const MPI_Datatype recvtypes[], MPI_Comm comm, MPI_Request *request|
os|Put|const void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win|s=EB(origin_count,origin_datatype) peer=target_rank
os|Get|void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win|r=EB(origin_count,origin_datatype) peer=target_rank
os|Accumulate|const void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Op op, MPI_Win win|s=EB(origin_count,origin_datatype) peer=target_rank
os|Get_accumulate|const void *origin_addr, int origin_count, MPI_Datatype origin_datatype, void *result_addr, int result_count, MPI_Datatype result_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Op op, MPI_Win win|s=EB(origin_count,origin_datatype) r=EB(result_count,result_datatype) peer=target_rank
os|Fetch_and_op|const void *origin_addr, void *result_addr, MPI_Datatype datatype, int target_rank, MPI_Aint target_disp, MPI_Op op, MPI_Win win|s=EB(1,datatype) r=EB(1,datatype) peer=target_rank
os|Compare_and_swap|const void *origin_addr, const void *compare_addr, void *result_addr, MPI_Datatype datatype, int target_rank, MPI_Aint target_disp, MPI_Win win|s=EB(1,datatype) r=EB(1,datatype) peer=target_rank
ros|Rput|const void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win, MPI_Request *request|s=EB(origin_count,origin_datatype) peer=target_rank
ros|Rget|void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win, MPI_Request *request|r=EB(origin_count,origin_datatype) peer=target_rank
ros|Raccumulate|const void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Op op, MPI_Win win, MPI_Request *request|s=EB(origin_count,origin_datatype) peer=target_rank
ros|Rget_accumulate|const void *origin_addr, int origin_count, MPI_Datatype origin_datatype, void *result_addr, int result_count, MPI_Datatype result_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Op op, MPI_Win win, MPI_Request *request|s=EB(origin_count,origin_datatype) r=EB(result_count,result_datatype) peer=target_rank
'''

# COLLTYPE numbers of ompi/mca/coll/base/coll_base_functions.h (blocking and non-blocking operation share the number)
OPID = {'Allgather': 0, 'Allgatherv': 1, 'Allreduce': 2, 'Alltoall': 3, 'Alltoallv': 4, 'Alltoallw': 5, 'Barrier': 6,
        'Bcast': 7, 'Exscan': 8, 'Gather': 9, 'Gatherv': 10, 'Reduce': 11, 'Reduce_scatter': 12,
        'Reduce_scatter_block': 13, 'Scan': 14, 'Scatter': 15, 'Scatterv': 16}
for _k in list(OPID):
    OPID['I' + _k[0].lower() + _k[1:]] = OPID[_k]


def pname(p):
    p = p.strip()
    m = re.search(r'(\w+)(\[\])?$', p)
    return m.group(1)

def main(out):
    L = [ '/* GENERATED by tools/gen_pmpi.py from its table - do not edit. */',
          '#include <mpi.h>', '#include "wrap.h"', '' ]
    n = 0
    lines = T.strip().splitlines()
    for line in list(lines):                       # MPI 4: every blocking collective has a persistent form
        k, nm, pr, at = [x.strip() for x in line.split('|')]
        if 'coll' == k:
            lines.append('pcoll|%s_init|%s, MPI_Info info, MPI_Request *request|%s v=4' % (nm, pr, at))
    for line in lines:
        kind, name, params, attrs = [x.strip() for x in line.split('|')]
        a = dict(x.split('=', 1) for x in attrs.split() if '=' in x) if attrs else {}
        names = [pname(p) for p in params.split(',')]
        win = 'win' in names
        comm = a.get('comm', 'MPI_COMM_NULL' if win else 'comm')
        s = a.get('s', '0'); r = a.get('r', '0')
        peer = a.get('peer'); src = a.get('src'); st = a.get('st')
        persist = kind in ('pinit', 'pcoll')
        blocking = kind in ('send', 'recv', 'sr', 'coll', 'os', 'pinit', 'pcoll')
        isreq = kind in ('isend', 'irecv', 'isr', 'icoll', 'ros')
        isrecv = kind in ('recv', 'irecv', 'sr', 'isr')   # pinit receives have no status: handled below
        flags = []
        if blocking: flags.append('EDUMPI_F_BLOCKING')
        if kind in ('coll', 'icoll', 'pcoll'): flags.append('EDUMPI_F_COLLECTIVE')
        if kind in ('os', 'ros'): flags.append('EDUMPI_F_ONESIDED')
        if src: flags.append('(%s==MPI_ANY_SOURCE?EDUMPI_F_ANY_SOURCE:0)' % src)
        fl = '|'.join(flags) or '0'
        cid = 'edumpi_wid(win)' if win else ('EDUMPI_NO_COMM' if comm == 'MPI_COMM_NULL' else 'edumpi_cid(%s)' % comm)
        call_args = list(names)
        o = []
        if a.get('v') == '4':
            o.append('#if MPI_VERSION >= 4')
        o.append('int MPI_%s(%s)' % (name, params))
        o.append('{')
        o.append('    static uint16_t fid_ = 0xFFFF;')
        o.append('    int rc_;')
        if st or kind in ('sr',):
            o.append('    MPI_Status local_;')
        if (blocking and isrecv):
            sname = st
            o.append('    MPI_Status *st_ = (%s == MPI_STATUS_IGNORE) ? &local_ : %s;' % (sname, sname))
            call_args = [('st_' if x == sname else x) for x in call_args]
        o.append('    edumpi_rec_t *r_ = NULL;')
        o.append('    if (!edumpi_enabled) {')
        o.append('        return PMPI_%s(%s);' % (name, ', '.join(names)))
        o.append('    }')
        if kind in ('icoll', 'pcoll'):
            o.append('    if (!edumpi_mpit_req_on) {')
            o.append('        edumpi_mpit_need_coll_requests();')
            o.append('    }')
        o.append('    r_ = edumpi_rec_begin("MPI_%s", &fid_, %s, %s);' % (name, cid, fl))
        iscoll = kind in ('coll', 'icoll')
        pcoll_op = OPID.get(name[:-5], 0xFFFFFFFF) if kind == 'pcoll' else 0xFFFFFFFF
        if iscoll:
            o.append('    if (r_) {')
            o.append('        r_->coll_op = %s;' % OPID.get(name, 0xFFFFFFFF))
            o.append('    }')
        o.append('    edumpi_tls_cur = r_;')
        if kind == 'pcoll':
            o.append('    edumpi_tls_pinit = 1;')
        if iscoll:
            o.append('    edumpi_tls_op = %s;' % OPID.get(name, -1))
        o.append('    rc_ = PMPI_%s(%s);' % (name, ', '.join(call_args)))
        o.append('    edumpi_tls_cur = NULL;')
        if kind == 'pcoll':
            o.append('    edumpi_tls_pinit = 0;')
        if iscoll:
            o.append('    edumpi_tls_op = -1;')
        o.append('    if (r_) {')
        o.append('        r_->end_ns = edumpi_now_ns();')
        if iscoll and (s != '0' or r != '0'):
            o.append('        if (!(r_->flags & EDUMPI_F_MPIT_SUM)) {   /* else: sums over the internal messages */')
            if s != '0': o.append('            r_->send_bytes = %s;' % s)
            if r != '0': o.append('            r_->recv_bytes = %s;' % r)
            o.append('        }')
        else:
            if s != '0': o.append('        r_->send_bytes = %s;' % s)
            if r != '0': o.append('        r_->recv_bytes = %s;' % r)
        if peer:
            o.append('        r_->partner = %s;' % ('edumpi_wworld(win, %s)' % peer if win else 'edumpi_world(%s, %s)' % (comm, peer)))
        if blocking and isrecv:
            o.append('        if (MPI_SUCCESS == rc_) {')
            o.append('            int n_ = 0;')
            if not peer:   # a call with a destination (Sendrecv) keeps it as its partner, as the fork does
                o.append('            r_->partner = edumpi_world(%s, st_->MPI_SOURCE);' % comm)
            o.append('            PMPI_Get_count(st_, MPI_BYTE, &n_);')
            o.append('            r_->recv_bytes = n_;')
            o.append('        }')
        if isreq:
            kindc = {'isend': 'EDUMPI_REQ_SEND', 'irecv': 'EDUMPI_REQ_RECV', 'isr': 'EDUMPI_REQ_RECV',
                     'icoll': 'EDUMPI_REQ_COLL', 'ros': 'EDUMPI_REQ_WIN'}[kind]
            o.append('        if (MPI_SUCCESS == rc_) {')
            o.append('            edumpi_req_add(*request, r_, %s, %s);' % (comm, kindc))
            o.append('        } else {')
            o.append('            edumpi_complete(r_);')
            o.append('        }')
        else:
            o.append('        edumpi_complete(r_);')
        o.append('    }')
        if persist:
            kc = {'pinit': ('EDUMPI_REQ_RECV' if src else 'EDUMPI_REQ_SEND'), 'pcoll': 'EDUMPI_REQ_COLL'}[kind]
            pw = ('edumpi_world(%s, %s)' % (comm, src)) if src else (('edumpi_world(%s, %s)' % (comm, peer)) if peer else '-1')
            o.append('    if (MPI_SUCCESS == rc_ && edumpi_enabled) {')
            o.append('        edumpi_persist_add(*request, %s, %s, %s, %s, %s, %s, %s);' % (
                comm, kc, s if kind == 'pcoll' or not src else '0', r if kind == 'pcoll' else '0', pw,
                ('(%s==MPI_ANY_SOURCE)' % src) if src else '0', ('(int) ' + str(pcoll_op)) if kind == 'pcoll' else '-1'))
            o.append('    }')
        o.append('    return rc_;')
        o.append('}')
        if a.get('v') == '4':
            o.append('#endif')
        o.append('')
        L += o
        n += 1
    open(out, 'w').write('\n'.join(L))
    print('generated', n, 'wrappers')

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'src/pmpi/wrappers.c')
