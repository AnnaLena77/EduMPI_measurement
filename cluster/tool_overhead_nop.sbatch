#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --exclusive
#SBATCH --time=0:40:00
#SBATCH -J tool-ovh-nop
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# Overhead by registered event set (EDUMPI_EVENT_MASK): none, pmpi, all (0x1ff), nomatch (0x11f), nomatch-noreq (0x01f), min (0x01b), algo (0x010)
# Overhead of the tool: no tool / PMPI only / MPI_T (file sink), pml ucx and pml ob1, 2 nodes x 2 ranks, optimized library.
# usage: sbatch tool_overhead.sbatch [mpi-install-name]  (default main-rel built from upstream: tool needs the fork's events -> use a release build of the fork)
NAME=${1:-dev-rel}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/ovh-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so bench 2>&1 | grep -E "error|warning" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
lscpu | grep -E "Model name|Thread\(s\) per core|Core\(s\) per socket|Socket\(s\)"
mkdir -p run && cd run
# The tool runs a monitor thread per rank. Without SMT it needs a core of its own: every rank gets 2 cores (PE=2),
# also in the runs without tool, so that the binding is the same in all columns.
BIND="--map-by ppr:2:node:PE=2 --bind-to core"
for pml in ucx ob1; do
  for mode in pmpi all all-nop algo algo-nop; do
    for rep in 1 2 3; do
      L="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=."
      case $mode in
        none) ENVS="" ;;
        all) ENVS="$L -x EDUMPI_EVENT_MASK=0x1ff" ;;
        all-nop) ENVS="$L -x EDUMPI_EVENT_MASK=0x1ff -x EDUMPI_CB_NOP=1" ;;
        algo-nop) ENVS="$L -x EDUMPI_EVENT_MASK=0x010 -x EDUMPI_CB_NOP=1" ;;
        pmpi) ENVS="$L -x EDUMPI_MPIT=0" ;;
        all) ENVS="$L -x EDUMPI_EVENT_MASK=0x1ff" ;;
        nomatch) ENVS="$L -x EDUMPI_EVENT_MASK=0x11f" ;;
        nomatch-noreq) ENVS="$L -x EDUMPI_EVENT_MASK=0x01f" ;;
        min) ENVS="$L -x EDUMPI_EVENT_MASK=0x01b" ;;
        algo) ENVS="$L -x EDUMPI_EVENT_MASK=0x010" ;;
      esac
      echo -n "$pml $mode run$rep: "
      mpirun $BIND --mca pml $pml $ENVS ../bench 20000 2>&1 | grep RESULT
    done
  done
done
ls -l edumpi.0.bin | awk '{print "log file of rank 0 (last run): " $5 " bytes"}'
