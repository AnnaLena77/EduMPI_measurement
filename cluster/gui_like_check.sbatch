#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=2
#SBATCH --exclusive
#SBATCH --time=0:20:00
#SBATCH -J gui-like
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# WP-V0 test: builds the GUI prefix and runs a job script written exactly like the one EduMPI_GUI writes
# (controller.cpp, writeRemoteBashFile, option 0), against a private PostgreSQL server on 127.0.0.1.
# usage: sbatch gui_like_check.sbatch [ompi-install-name]
NAME=${1:-dev-rel}
OMPI=/data/fdai0231/edumpi-t/install/$NAME
GUI=/data/fdai0231/edumpi-t/gui/test-$SLURM_JOB_ID
bash $HOME/edumpi_t/tool/cluster/make_gui_prefix.sh $OMPI $GUI $HOME/edumpi_t/tool || exit 1
export PATH=$(dirname $(readlink -f $(which initdb))):$PATH
PGBIN=$(dirname $(readlink -f $(which initdb)))
PGD=/tmp/pg-$SLURM_JOB_ID; PORT=54331; HOST=127.0.0.1
$PGBIN/initdb -D $PGD -U edumpi --auth=trust -E UTF8 --locale=C 2>&1 | tail -n 2
$PGBIN/pg_ctl -D $PGD -o "-p $PORT -c listen_addresses=127.0.0.1 -c unix_socket_directories=$PGD" -l $PGD/log -w start >/dev/null || { cat $PGD/log; exit 1; }
psql -h $HOST -p $PORT -U edumpi -d postgres -qc "create database edumpi" && psql -h $HOST -p $PORT -U edumpi -d edumpi -q -f $HOME/edumpi_t/tool/tests/sql/schema_plain.sql
D=/data/fdai0231/edumpi-t/toolrun/gui-$SLURM_JOB_ID; mkdir -p $D/tmp; cd $D
cp $HOME/edumpi_t/tool/tests/test_calls.c prog.c
printf 'DB_HOST=%s\nDB_NAME=edumpi\nDB_PORT=%s\nDB_USER=edumpi\nDB_PW=x\n' $HOST $PORT > tmp/envfile
# ---- the lines of the GUI script (option 0), cluster path = $GUI ----
for i in {1..5}; do
    if source ./tmp/envfile 2>/dev/null; then
        export $(cut -d= -f1 ./tmp/envfile)
        rm ./tmp/envfile
        break
    else
        sleep 1
    fi
done
export OMPI_MCA_coll_han_priority=0
export EDUMPI_PROGRAM=prog.c
$GUI/bin/mpicc prog.c -o prog -lm
$GUI/bin/mpirun -n 4 --map-by :PE=2 --mca pml ob1 ./prog
# ---- end of the GUI lines ----
psql -h $HOST -p $PORT -U edumpi -d edumpi -f $HOME/edumpi_t/tool/tests/sql/check_calls.sql
psql -h $HOST -p $PORT -U edumpi -d edumpi -c "select count(*), count(distinct processrank), min(edumpi_run_id) from edumpi_running_data" 2>&1 | head
$PGBIN/pg_ctl -D $PGD -m fast stop >/dev/null; rm -rf $PGD
