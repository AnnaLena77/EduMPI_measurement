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
make CC=mpicc libedumpi.so test_store test_init test_calls test_late 2>&1 | grep -E "error|warning" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
./test_store . 8 500000
mkdir -p run && cd run
echo "== init/finalize only"
LD_PRELOAD=$W/libedumpi.so EDUMPI_LOG_DIR=. mpirun --map-by ppr:2:node ../test_init
for mode in "ob1:--mca pml ob1:" "ucx:--mca pml ucx:" "ob1-nohan:--mca pml ob1 --mca coll ^han:" "ob1-pmpi-only:--mca pml ob1:EDUMPI_MPIT=0"; do
  name=${mode%%:*}; rest=${mode#*:}; margs=${rest%%:*}; envs=${rest#*:}
  echo "== test_calls, $name (2 nodes x 2 ranks)"
  rm -f edumpi.*.bin
  env EDUMPI_DEBUG=1 $envs LD_PRELOAD=$W/libedumpi.so EDUMPI_LOG_DIR=. mpirun --map-by ppr:2:node $margs -x LD_PRELOAD -x EDUMPI_LOG_DIR -x EDUMPI_DEBUG $( [ -n "$envs" ] && echo -x ${envs%%=*} ) ../test_calls
  if [ "$name" = ob1-pmpi-only ]; then EXPECT_MPIT=0 python3 ../tests/check_calls.py . 4 | tail -n 1; else EXPECT_MPIT=1 python3 ../tests/check_calls.py . 4 | grep -E "FAIL|RESULT|algorithms|detail"; fi
done
echo "== late sender / late receiver (ob1 over tcp, 2 nodes x 1 rank)"
rm -f edumpi.*.bin
LD_PRELOAD=$W/libedumpi.so EDUMPI_LOG_DIR=. mpirun -n 2 --map-by ppr:1:node --mca pml ob1 --mca btl self,tcp -x LD_PRELOAD -x EDUMPI_LOG_DIR ../test_late
python3 ../tests/check_late.py .
python3 ../tools/edumpi_dump.py edumpi.1.bin | head -n 12 | cut -c1-220
