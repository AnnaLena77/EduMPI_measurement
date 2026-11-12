#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --exclusive
#SBATCH --time=0:40:00
#SBATCH -J tool-vs-fork
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# G5, differential test: the same test programs run (a) with the original EduMPI fork (Open MPI 5.0.11 with qentry,
# read-only use of ~/EduMPI_Testumgebung) and (b) with the new tool on the MPI_T fork build. Both write to a private
# PostgreSQL server on 127.0.0.1 only (two databases); the rows are reduced to what is deterministic (no times) and diffed.
# usage: sbatch tool_vs_fork.sbatch [mpi-install-name]
NAME=${1:-dev-rel}
NEWP=/data/fdai0231/edumpi-t/install/$NAME
OLDP=$HOME/EduMPI_Testumgebung
PQLIB=$(dirname $(readlink -f /etc/profiles/per-group/cluster/lib/libpq.so)); PQINC=/data/fdai0231/edumpi-t/pq-include
W=/data/fdai0231/edumpi-t/toolrun/vs-$SLURM_JOB_ID; mkdir -p $W/old $W/new; cp -r $HOME/edumpi_t/tool/. $W/; cd $W
export PMIX_MCA_pcompress_base_silence_warning=1
PROGS="test_calls test_nbc"
# build: new side with the new mpicc and the tool; old side with the fork's mpicc
( export PATH=$NEWP/bin:$PATH OPAL_PREFIX=$NEWP LD_LIBRARY_PATH=$NEWP/lib
  make clean >/dev/null; make CC=mpicc PG=1 PQINC=$PQINC PQLIB=$PQLIB libedumpi.so $PROGS 2>&1 | grep -E "error" | head
  for p in $PROGS; do mv $p new/; done )
( export PATH=$OLDP/bin:$PATH OPAL_PREFIX=$OLDP LD_LIBRARY_PATH=$OLDP/lib
  for p in $PROGS; do mpicc -O1 -o old/$p tests/$p.c 2>&1 | grep -E "error" | head; done )
test -f libedumpi.so && ls old new
PGBIN=$(dirname $(readlink -f $(which initdb)))
PGD=/tmp/pg-$SLURM_JOB_ID; PORT=54331; HOST=127.0.0.1
$PGBIN/initdb -D $PGD -U edumpi --auth=trust -E UTF8 --locale=C >/dev/null 2>&1
$PGBIN/pg_ctl -D $PGD -o "-p $PORT -c listen_addresses=127.0.0.1 -c unix_socket_directories=$PGD" -l $PGD/log -w start >/dev/null || { cat $PGD/log; exit 1; }
for d in old new; do psql -h $HOST -p $PORT -U edumpi -d postgres -qc "create database edumpi_$d" && psql -h $HOST -p $PORT -U edumpi -d edumpi_$d -q -f tests/sql/schema_plain.sql; done
MCA="--mca pml ob1 --mca btl self,sm"
export DB_HOST=$HOST DB_PORT=$PORT DB_USER=edumpi DB_PW=x
for p in $PROGS; do
  echo "== $p, original fork"
  ( export PATH=$OLDP/bin:$PATH OPAL_PREFIX=$OLDP LD_LIBRARY_PATH=$OLDP/lib DB_NAME=edumpi_old EDUMPI_PROGRAM=$p
    mpirun -n 4 $MCA -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x EDUMPI_PROGRAM -x SLURM_JOBID old/$p 2>&1 | tail -n 4 )
  echo "== $p, new tool"
  ( export PATH=$NEWP/bin:$PATH OPAL_PREFIX=$NEWP LD_LIBRARY_PATH=$NEWP/lib DB_NAME=edumpi_new EDUMPI_PROGRAM=$p EDUMPI_SINK=pg LD_PRELOAD=$W/libedumpi.so
    mpirun -n 4 $MCA -x LD_PRELOAD -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x EDUMPI_PROGRAM -x EDUMPI_SINK -x SLURM_JOBID new/$p 2>&1 | tail -n 4 )
done
Q='select processrank, "function", communicationtype, communicationarea, senddatasize, recvdatasize, partnerrank, coll_algorithm, coll_partnerranks, count(*) from edumpi_running_data group by 1,2,3,4,5,6,7,8,9 order by 1,2,3,4,5,6,7,8,9'
for d in old new; do psql -h $HOST -p $PORT -U edumpi -d edumpi_$d -At -F '|' -c "$Q" > rows_$d.txt; wc -l rows_$d.txt; done
echo "== differences per (rank, function)"
python3 tests/compare_rows.py rows_old.txt rows_new.txt
cut -d'|' -f2 rows_old.txt | sort | uniq -c > f_old.txt; cut -d'|' -f2 rows_new.txt | sort | uniq -c > f_new.txt
echo "== functions only in one side"; diff f_old.txt f_new.txt | head -30
$PGBIN/pg_ctl -D $PGD -m fast stop >/dev/null; rm -rf $PGD
