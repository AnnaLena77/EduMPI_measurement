#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --time=0:15:00
#SBATCH -J tool-algo
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# how many ompi.coll_algorithm_selected events does one Allreduce / Bcast raise (HAN), per rank?
NAME=${1:-dev-rel}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/algo-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so bench 2>&1 | grep -E "error" | head
mkdir -p run && cd run
for pml in ucx ob1; do
  echo "== $pml"
  mpirun --map-by ppr:2:node:PE=2 --bind-to core --mca pml $pml -x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=. -x EDUMPI_EVENT_MASK=0x010 -x EDUMPI_DEBUG=1 ../bench 2000 2>&1 | grep -E "RESULT|events|created"
done
