#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=2
#SBATCH --exclusive
#SBATCH --time=0:15:00
#SBATCH -J pg-tz
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# Time zones: the compute nodes run on Europe/Berlin, a hosted database often on UTC. Is edumpi_runs.start_time / end_time
# consistent with the times of the rows (time_start / time_end) when the server zone differs from the node zone?
# usage: sbatch pg_tz_check.sbatch [ompi-install-name] [server time zone, default UTC] [tool dir]
NAME=${1:-dev-rel}; SRVTZ=${2:-UTC}; TOOL=${3:-$HOME/edumpi_t/tool}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
PQLIB=$(dirname $(readlink -f /etc/profiles/per-group/cluster/lib/libpq.so)); PQINC=/data/fdai0231/edumpi-t/pq-include
W=/data/fdai0231/edumpi-t/toolrun/tz-$SLURM_JOB_ID; mkdir -p $W; cp -r $TOOL/. $W/; cd $W; make clean >/dev/null
make CC=mpicc PG=1 PQINC=$PQINC PQLIB=$PQLIB libedumpi.so 2>&1 | grep -E "error|warning" | head
mpicc -o test_live tests/test_live.c || exit 1
PGBIN=$(dirname $(readlink -f $(which initdb)))
PGD=/tmp/pg-$SLURM_JOB_ID; PORT=54334; HOST=127.0.0.1
$PGBIN/initdb -D $PGD -U edumpi --auth=trust -E UTF8 --locale=C 2>&1 | tail -n 1
$PGBIN/pg_ctl -D $PGD -o "-p $PORT -c listen_addresses=127.0.0.1 -c unix_socket_directories=$PGD -c timezone=$SRVTZ" -l $PGD/log -w start >/dev/null || { cat $PGD/log; exit 1; }
psql -h $HOST -p $PORT -U edumpi -d postgres -qc "create database edumpi" && psql -h $HOST -p $PORT -U edumpi -d edumpi -q -f tests/sql/schema_plain.sql
export DB_HOST=$HOST DB_PORT=$PORT DB_USER=edumpi DB_NAME=edumpi DB_PW=x EDUMPI_PROGRAM=test_live EDUMPI_SINK=pg
echo "node: $(date +%Z) server zone: $SRVTZ"
mpirun -n 4 --map-by slot:PE=2 --mca pml ob1 -x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_SINK -x EDUMPI_PROGRAM \
    -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x SLURM_JOBID ./test_live
Q="psql -h $HOST -p $PORT -U edumpi -d edumpi -At"
echo "== run row and rows (shown in UTC)"
$Q -c "set timezone='UTC'; select 'start_time ' || start_time, 'end_time ' || end_time from edumpi_runs" | grep -v '^SET'
$Q -c "set timezone='UTC'; select 'first row ' || min(time_start), 'last row end ' || max(time_end) from edumpi_running_data" | grep -v '^SET'
echo "== offsets in seconds (should be about 0 for start and about 0 or slightly positive for end)"
$Q -c "select 'start_time - first row: ' || round(extract(epoch from (r.start_time - d.a))::numeric,1), 'end_time - last row: ' || round(extract(epoch from (r.end_time - d.b))::numeric,1) from edumpi_runs r, (select min(time_start) a, max(time_end) b from edumpi_running_data) d"
$PGBIN/pg_ctl -D $PGD -m fast stop >/dev/null; rm -rf $PGD
