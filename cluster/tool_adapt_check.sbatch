#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --time=0:20:00
#SBATCH -J tool-adapt
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# coll/adapt Ibcast/Ireduce with the MPI_T backend: attribution of the internal messages and generic request events
# usage: sbatch tool_adapt_check.sbatch [mpi-install-name]
NAME=${1:-dev-rel}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so test_adapt 2>&1 | grep -E "error|warning" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
mkdir -p run && cd run
for pml in ob1 ucx; do
  echo "== coll/adapt, pml $pml (2 nodes x 2 ranks)"
  rm -f edumpi.*.bin
  EDUMPI_DEBUG=1 LD_PRELOAD=$W/libedumpi.so EDUMPI_LOG_DIR=. mpirun --map-by ppr:2:node --mca pml $pml --mca coll_adapt_priority 100 -x LD_PRELOAD -x EDUMPI_LOG_DIR -x EDUMPI_DEBUG ../test_adapt 2>&1 | grep -v "^edumpi mpit" | head -n 5
  python3 ../tests/check_adapt.py . 4 | grep -E "FAIL|RESULT" | head -n 20
  python3 ../tools/edumpi_dump.py edumpi.1.bin | grep -E "Ibcast|Ireduce" | head -n 6 | cut -c1-200
done
