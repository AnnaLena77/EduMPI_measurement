#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --ntasks=2
#SBATCH --cpus-per-task=2
#SBATCH --exclusive
#SBATCH --time=0:15:00
#SBATCH -J threadless
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# WP-V2: the file variant without a monitor thread. (1) store test in both modes on x86 (TSC clock renewed inline), (2) thread count
# of a real MPI process: none / EDUMPI_THREAD=0 / EDUMPI_THREAD=1, (3) the file of the thread-less run holds every call.
# usage: sbatch threadless_check.sbatch [ompi-install-name] [tool dir]
NAME=${1:-dev-rel}; TOOL=${2:-$HOME/edumpi_t/tool}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/tl-$SLURM_JOB_ID; mkdir -p $W; cp -r $TOOL/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so 2>&1 | grep -E "error|warning" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
cc -O2 -Wall -pthread -o test_store tests/test_store.c src/core/store.c src/core/file_sink.c src/core/tee_sink.c src/core/pg_sink.c || exit 1
mpicc -o tc tests/test_threads_count.c || exit 1
echo "== store test, threaded and thread-less (8 threads, 1.6 million records)"
for m in "" "TS_THREADLESS=1"; do rm -rf st; mkdir st; echo "-- ${m:-threaded}"; env $m ./test_store st 8 200000 2>&1 | tail -4; done
BIND="--map-by slot:PE=2"
echo "== threads of rank 0 (the baseline has the threads of the MPI library itself)"
mkdir -p run && cd run
echo -n "no tool:            "; EDUMPI_DISABLE=1 LD_PRELOAD=$W/libedumpi.so mpirun -n 2 $BIND -x LD_PRELOAD -x EDUMPI_DISABLE ../tc | grep THREADS
echo -n "file, EDUMPI_THREAD=0: "; rm -f edumpi.*; EDUMPI_THREAD=0 EDUMPI_SINK=file EDUMPI_LOG_DIR=. mpirun -n 2 $BIND -x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_THREAD -x EDUMPI_SINK -x EDUMPI_LOG_DIR ../tc | grep THREADS
for r in 0 1; do echo "rank $r records in the file: $(python3 $W/tools/edumpi_dump.py edumpi.$r.bin | tail -n +2 | grep -c MPI_Allreduce) MPI_Allreduce (expect 5000)"; done
echo -n "file, EDUMPI_THREAD=1: "; rm -f edumpi.*; EDUMPI_THREAD=1 EDUMPI_SINK=file EDUMPI_LOG_DIR=. mpirun -n 2 $BIND -x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_THREAD -x EDUMPI_SINK -x EDUMPI_LOG_DIR ../tc | grep THREADS
for r in 0 1; do echo "rank $r records in the file: $(python3 $W/tools/edumpi_dump.py edumpi.$r.bin | tail -n +2 | grep -c MPI_Allreduce) MPI_Allreduce (expect 5000)"; done
echo "-- default (file sink, no EDUMPI_THREAD): "; rm -f edumpi.*; EDUMPI_SINK=file EDUMPI_LOG_DIR=. mpirun -n 2 $BIND -x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_SINK -x EDUMPI_LOG_DIR ../tc | grep THREADS
