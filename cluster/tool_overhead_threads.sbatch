#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --exclusive
#SBATCH --time=0:50:00
#SBATCH -J tool-ovh-thr
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# WP-V2: overhead of the file sink with and without monitor thread, with 1 and 2 cores per rank.
# columns: none/PE2, none/PE1, pmpi/PE1 (no thread), thread/PE2, thread/PE1, threadless/PE2, threadless/PE1, immediate/PE1; pml ucx and ob1.
# threadless: records are held in memory (EDUMPI_MEM_MB, default 256) and written at MPI_Finalize; immediate: EDUMPI_MEM_MB=0.
# usage: sbatch tool_overhead_threads.sbatch [mpi-install-name]
NAME=${1:-dev-rel}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/ovt-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so bench 2>&1 | grep -E "error|warning" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
lscpu | grep -E "Model name|Thread\(s\) per core"
mkdir -p run && cd run
for pml in ucx ob1; do
  for mode in none-pe2 none-pe1 pmpi-pe1 thread-pe2 thread-pe1 threadless-pe2 threadless-pe1 immediate-pe1; do
    case $mode in *-pe1) BIND="--map-by ppr:2:node:PE=1 --bind-to core" ;; *) BIND="--map-by ppr:2:node:PE=2 --bind-to core" ;; esac
    L="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=. -x EDUMPI_SINK=file"
    case $mode in
      none-*) ENVS="" ;;
      pmpi-*) ENVS="$L -x EDUMPI_THREAD=0 -x EDUMPI_MPIT=0" ;;
      thread-*) ENVS="$L -x EDUMPI_THREAD=1" ;;
      threadless-*) ENVS="$L -x EDUMPI_THREAD=0" ;;      # default: records stay in memory (EDUMPI_MEM_MB=256), written at finalize
      immediate-*) ENVS="$L -x EDUMPI_THREAD=0 -x EDUMPI_MEM_MB=0" ;;   # the application thread writes every segment
    esac
    for rep in 1 2 3; do
      echo -n "$pml $mode run$rep: "
      mpirun $BIND --mca pml $pml $ENVS ../bench 20000 2>&1 | grep RESULT
    done
  done
done
