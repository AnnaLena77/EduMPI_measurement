#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=2
#SBATCH --exclusive
#SBATCH --time=0:20:00
#SBATCH -J pg-live
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# Visibility test of the PostgreSQL sink: how long after a call does another session see its row? Private PostgreSQL on
# 127.0.0.1; tests/test_live.c runs 8 s with few calls; a second session counts the rows every 0.5 s while it runs.
# usage: sbatch pg_live_check.sbatch [ompi-install-name] [EDUMPI_FLUSH_MS] [tool dir]
NAME=${1:-dev-rel}; FLUSH=${2:-500}; TOOL=${3:-$HOME/edumpi_t/tool}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
PQLIB=$(dirname $(readlink -f /etc/profiles/per-group/cluster/lib/libpq.so)); PQINC=/data/fdai0231/edumpi-t/pq-include
W=/data/fdai0231/edumpi-t/toolrun/live-$SLURM_JOB_ID; mkdir -p $W; cp -r $TOOL/. $W/; cd $W; make clean >/dev/null
make CC=mpicc PG=1 PQINC=$PQINC PQLIB=$PQLIB libedumpi.so 2>&1 | grep -E "error|warning" | head
mpicc -o test_live tests/test_live.c || exit 1
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
PGBIN=$(dirname $(readlink -f $(which initdb)))
PGD=/tmp/pg-$SLURM_JOB_ID; PORT=54332; HOST=127.0.0.1
$PGBIN/initdb -D $PGD -U edumpi --auth=trust -E UTF8 --locale=C 2>&1 | tail -n 1
$PGBIN/pg_ctl -D $PGD -o "-p $PORT -c listen_addresses=127.0.0.1 -c unix_socket_directories=$PGD" -l $PGD/log -w start >/dev/null || { cat $PGD/log; exit 1; }
psql -h $HOST -p $PORT -U edumpi -d postgres -qc "create database edumpi" && psql -h $HOST -p $PORT -U edumpi -d edumpi -q -f tests/sql/schema_plain.sql
export DB_HOST=$HOST DB_PORT=$PORT DB_USER=edumpi DB_NAME=edumpi DB_PW=x EDUMPI_PROGRAM=test_live EDUMPI_SINK=pg EDUMPI_FLUSH_MS=$FLUSH
( for i in $(seq 1 40); do
    n=$(psql -h $HOST -p $PORT -U edumpi -d edumpi -Atc "select count(*) from edumpi_running_data")
    echo "t=$(date +%s.%N | cut -c1-14) rows=$n"; sleep 0.5; done ) > poll.log &
POLL=$!
T0=$(date +%s.%N)
mpirun -n 4 --map-by slot:PE=2 --mca pml ob1 -x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_SINK -x EDUMPI_PROGRAM -x EDUMPI_FLUSH_MS \
    -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x SLURM_JOBID ./test_live
T1=$(date +%s.%N)
sleep 1; kill $POLL 2>/dev/null
echo "program ran $(awk -v a=$T0 -v b=$T1 "BEGIN{printf \"%.1f\", b-a}") s, flush interval $FLUSH ms"
echo "== rows seen by a second session (seconds since program start)"
awk -v t0=$T0 '{split($1,a,"="); split($2,b,"="); printf "%6.1f s  rows=%s\n", a[2]-t0, b[2]}' poll.log | awk '!seen[$3]++' 
echo "== final"
psql -h $HOST -p $PORT -U edumpi -d edumpi -Atc "select count(*), count(*) filter (where \"function\"='MPI_Irecv') from edumpi_running_data"
$PGBIN/pg_ctl -D $PGD -m fast stop >/dev/null; rm -rf $PGD
