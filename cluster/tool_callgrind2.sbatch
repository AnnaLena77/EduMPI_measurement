#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --exclusive
#SBATCH --time=0:40:00
#SBATCH -J tool-cg2
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# Where do the cycles of the MPI_T backend go? callgrind of 2 ranks on one node (ob1/sm), bench with few iterations.
# 2 nodes x 2 ranks (HAN is active), ob1 over tcp.
# usage: sbatch tool_callgrind.sbatch [mpi-install-name] [event-mask]
NAME=${1:-dev-rel}; MASK=${2:-0x1ff}; NP=${3:-2}; XMCA=${4:-}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/cg-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc CFLAGS="-O2 -g -fPIC" libedumpi.so bench 2>&1 | grep -E "error" | head
mkdir -p run && cd run
mpirun -n $NP --map-by ppr:2:node --bind-to none --mca pml ob1 --mca btl self,sm,tcp $XMCA -x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=. -x EDUMPI_EVENT_MASK=$MASK \
  valgrind --tool=callgrind --callgrind-out-file=$W/run/cg.%p.out ../bench 1000 2>&1 | grep -E "RESULT|Collected|error" | head
for f in $(ls -S cg.*.out | head -n 2); do
  echo "=== $f (inclusive, top 40)"
  callgrind_annotate --inclusive=yes $f 2>/dev/null | sed -n 20,70p | cut -c1-200
done
for f in $(ls -S cg.*.out | head -n 1); do
  echo "=== $f (self, top 30)"
  callgrind_annotate $f 2>/dev/null | sed -n 20,52p | cut -c1-200
done
