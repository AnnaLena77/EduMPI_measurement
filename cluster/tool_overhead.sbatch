#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --exclusive
#SBATCH --time=0:40:00
#SBATCH -J tool-ovh
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# Overhead of the tool: no tool / PMPI only / MPI_T (file sink), pml ucx and pml ob1, 2 nodes x 2 ranks, optimized library.
# usage: sbatch tool_overhead.sbatch [mpi-install-name]  (default main-rel built from upstream: tool needs the fork's events -> use a release build of the fork)
NAME=${1:-dev-rel}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/ovh-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so bench 2>&1 | grep -E "error|warning" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
mkdir -p run && cd run
for pml in ucx ob1; do
  for mode in none pmpi mpit mpit-fast; do
    for rep in 1 2 3; do
      case $mode in
        none) ENVS="" ;;
        pmpi) ENVS="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_MPIT=0 -x EDUMPI_LOG_DIR=." ;;
        mpit) ENVS="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=." ;;
        mpit-fast) ENVS="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=. --mca opal_mca_base_event_fast_clock 1" ;;
      esac
      echo -n "$pml $mode run$rep: "
      mpirun --map-by ppr:2:node --mca pml $pml $ENVS ../bench 20000 2>&1 | grep RESULT
    done
  done
done
ls -l edumpi.0.bin | awk '{print "log file of rank 0 (last run): " $5 " bytes"}'
