#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --ntasks-per-node=4
#SBATCH --time=0:20:00
#SBATCH -J tool-pg
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# End-to-end test of the PostgreSQL sink: a private PostgreSQL server on this node that listens on 127.0.0.1 only
# (default local-only access rules), the plain table definitions, test_calls with EDUMPI_SINK=both, SQL checks.
# usage: sbatch tool_pg_check.sbatch [mpi-install-name]
NAME=${1:-dev-dbg}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
PQLIB=$(dirname $(readlink -f /etc/profiles/per-group/cluster/lib/libpq.so)); PQINC=/data/fdai0231/edumpi-t/pq-include
W=/data/fdai0231/edumpi-t/toolrun/pg-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc PG=1 PQINC=$PQINC PQLIB=$PQLIB libedumpi.so test_calls 2>&1 | grep -E "error|warning" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
PGBIN=$(dirname $(readlink -f $(which initdb)))   # real store path: the profile symlinks lack the share directory
PGD=/tmp/pg-$SLURM_JOB_ID; PORT=54329; HOST=127.0.0.1
$PGBIN/initdb -D $PGD -U edumpi --auth=trust -E UTF8 --locale=C 2>&1 | tail -n 5
$PGBIN/pg_ctl -D $PGD -o "-p $PORT -c listen_addresses=127.0.0.1 -c unix_socket_directories=$PGD" -l $PGD/log -w start >/dev/null || { cat $PGD/log; exit 1; }
psql -h $HOST -p $PORT -U edumpi -d postgres -qc "create database edumpi" && psql -h $HOST -p $PORT -U edumpi -d edumpi -q -f tests/sql/schema_plain.sql
export DB_HOST=$HOST DB_PORT=$PORT DB_USER=edumpi DB_NAME=edumpi DB_PW=x EDUMPI_PROGRAM=test_calls EDUMPI_SINK=both
mkdir -p run && cd run
echo "== test_calls -> database and file"
LD_PRELOAD=$W/libedumpi.so EDUMPI_LOG_DIR=. mpirun -n 4 -x LD_PRELOAD -x EDUMPI_LOG_DIR -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x EDUMPI_PROGRAM -x EDUMPI_SINK -x SLURM_JOBID ../test_calls
psql -h $HOST -p $PORT -U edumpi -d edumpi -f ../tests/sql/check_calls.sql
echo "== sample rows"
psql -h $HOST -p $PORT -U edumpi -d edumpi -c "select \"function\", communicationtype, senddatasize, recvdatasize, partnerrank, coll_algorithm, time_diff from edumpi_running_data where processrank=1 order by time_end limit 8"
$PGBIN/pg_ctl -D $PGD -m fast stop >/dev/null; rm -rf $PGD
