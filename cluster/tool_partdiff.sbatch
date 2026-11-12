#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --exclusive
#SBATCH --time=1:00:00
#SBATCH -J tool-pd
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# Jacobi stencil (parcio partdiff, halo exchange with Isend/Recv/Wait every iteration): a program with a high rate of small MPI calls.
# 4 ranks on 2 nodes, 2 cores per rank; no tool, PMPI only, MPI_T default; ucx and ob1.
NAME=${1:-dev-rel}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/pd-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so 2>&1 | grep -E "error" | head
mpicc -O3 -o partdiff $HOME/parcio-partdiff-mpi/partdiff.c -lm 2>&1 | grep -E "error" | head
test -x partdiff || { echo BUILD FAILED; exit 1; }
mkdir -p run && cd run
BIND="--map-by ppr:2:node:PE=2 --bind-to core"
for cfg in "16 200000" "32 100000"; do
  set -- $cfg
  for pml in ucx ob1; do
    for mode in none pmpi mpit; do
      for rep in 1 2 3; do
        case $mode in
          none) ENVS="" ;;
          pmpi) ENVS="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_MPIT=0 -x EDUMPI_LOG_DIR=." ;;
          mpit) ENVS="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=." ;;
        esac
        echo -n "lines=$1 iters=$2 $pml $mode run$rep: "
        mpirun $BIND --mca pml $pml $ENVS ../partdiff 1 2 $1 2 2 $2 2>&1 | grep -E "Berechnungszeit|Calculation time|time" | tr -s ' ' | tr '\n' ' '; echo
      done
    done
  done
done
ls -l edumpi.0.bin | awk '{print "log file of rank 0 (last run): " $5 " bytes"}'
