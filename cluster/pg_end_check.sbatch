#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --ntasks=32
#SBATCH --cpus-per-task=2
#SBATCH --exclusive
#SBATCH --time=0:30:00
#SBATCH -J pg-end
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# What does a reader of the database see at the END of a run? Private PostgreSQL on 127.0.0.1, sink "both" (database and file),
# a user program (default: the program of a GUI run). Compares rows per rank in the database with the file, the end_time of the run
# with the last rows, and counts rows that end after end_time.
# usage: sbatch pg_end_check.sbatch <program.c> [gui-prefix] [ranks]
SRC=${1:?program.c}; GUI=${2:-/data/fdai0231/edumpi-t/gui/edumpi-t-db-2026-11-15}; NP=${3:-32}
OMPI=$(sed -n 's/^ompi prefix: //p' $GUI/VERSION)
export PATH=$OMPI/bin:$PATH OPAL_PREFIX=$OMPI LD_LIBRARY_PATH=$OMPI/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/end-$SLURM_JOB_ID; mkdir -p $W/run; cd $W/run; cp $SRC prog.c; mpicc prog.c -o prog -lm || exit 1
PGBIN=$(dirname $(readlink -f $(which initdb)))
PGD=/tmp/pg-$SLURM_JOB_ID; PORT=54333; HOST=127.0.0.1
$PGBIN/initdb -D $PGD -U edumpi --auth=trust -E UTF8 --locale=C 2>&1 | tail -n 1
$PGBIN/pg_ctl -D $PGD -o "-p $PORT -c listen_addresses=127.0.0.1 -c unix_socket_directories=$PGD -c max_connections=400" -l $PGD/log -w start >/dev/null || { cat $PGD/log; exit 1; }
psql -h $HOST -p $PORT -U edumpi -d postgres -qc "create database edumpi" && psql -h $HOST -p $PORT -U edumpi -d edumpi -q -f $HOME/edumpi_t/tool/tests/sql/schema_plain.sql
export DB_HOST=$HOST DB_PORT=$PORT DB_USER=edumpi DB_NAME=edumpi DB_PW=x EDUMPI_PROGRAM=prog.c EDUMPI_SINK=both EDUMPI_LOG_DIR=$W/run
T0=$(date +%s)
mpirun -n $NP --map-by slot:PE=2 --mca pml ob1 -x LD_PRELOAD=$GUI/lib/libedumpi.so -x EDUMPI_SINK -x EDUMPI_PROGRAM -x EDUMPI_LOG_DIR \
    -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x SLURM_JOBID ./prog 2>&1 | tail -3
T1=$(date +%s); echo "program wall time $((T1-T0)) s, ranks $NP"
Q="psql -h $HOST -p $PORT -U edumpi -d edumpi -At"
echo "== run row"; $Q -c "select start_time, end_time from edumpi_runs"
echo "== rows in the database / in the files"
DBN=$($Q -c "select count(*) from edumpi_running_data"); echo "database: $DBN"
FN=0; for f in edumpi.*.bin; do n=$(python3 $HOME/edumpi_t/tool/tools/edumpi_dump.py $f | tail -n +2 | wc -l); FN=$((FN+n)); done; echo "files: $FN (Isend_done rows are database only)"
$Q -c "select count(*) filter (where \"function\"='MPI_Isend_done') from edumpi_running_data" | sed 's/^/Isend_done rows: /'
echo "== ranks with rows: $($Q -c 'select count(distinct processrank) from edumpi_running_data') of $NP; cluster rows: $($Q -c 'select count(*) from edumpi_cluster_info')"
echo "== last row per rank (max time_end) relative to end_time, seconds (negative = before end_time)"
$Q -c "select round(min(extract(epoch from (m - e)))::numeric,2), round(max(extract(epoch from (m - e)))::numeric,2) from (select processrank, max(time_end) m from edumpi_running_data group by 1) r, (select end_time e from edumpi_runs) x"
echo "== rows that end after end_time: $($Q -c 'select count(*) from edumpi_running_data d, edumpi_runs r where d.time_end > r.end_time')"
echo "== last 5 rows of rank 0"
$Q -c "select \"function\", time_start, time_end, time_diff from edumpi_running_data where processrank=0 order by time_end desc limit 5"
$PGBIN/pg_ctl -D $PGD -m fast stop >/dev/null; rm -rf $PGD
