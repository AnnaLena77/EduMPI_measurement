#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --time=0:20:00
#SBATCH -J tool-chk
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# usage: sbatch tool_check.sbatch [mpi-install-name]   tool sources in ~/edumpi_t/tool (rsync of EduMPI_measurement)
NAME=${1:-dev-dbg}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null  # shared file system: all nodes must see the binaries
make CC=mpicc libedumpi.so test_store test_init test_calls 2>&1 | grep -E "error|warning" | head
./test_store . 8 500000
mkdir -p run && cd run
echo "== init/finalize only"
LD_PRELOAD=$W/libedumpi.so EDUMPI_LOG_DIR=. mpirun --map-by ppr:2:node ../test_init
echo "== test_calls (known traffic), 2 nodes x 2 ranks"
rm -f edumpi.*.bin
LD_PRELOAD=$W/libedumpi.so EDUMPI_LOG_DIR=. mpirun --map-by ppr:2:node ../test_calls
python3 ../tests/check_calls.py . 4
python3 ../tools/edumpi_dump.py edumpi.1.bin | head -n 12 | cut -c1-200
