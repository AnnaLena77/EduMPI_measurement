#!/usr/bin/env bash
#SBATCH -p all
#SBATCH -N 2
#SBATCH --ntasks-per-node=1
#SBATCH --time=10:00
#SBATCH -J probe
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# usage: sbatch run_probe.sbatch <install-prefix> <pml> "<extra mpirun args>" [nodes-layout: 2nodes|1node]
set -eo pipefail
P=${1:?prefix}; PML=${2:-ob1}; EXTRA=${3:-}; LAYOUT=${4:-2nodes}
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH
OUT=/data/fdai0231/edumpi-t/probe/$SLURM_JOB_ID; mkdir -p $OUT
cat $P/EDUMPI_T_COMMIT; date
mpicc -O1 -o $OUT/probe $HOME/edumpi_t/probe/mpit_ob1_probe.c
ompi_info | grep -E "MCA (pml|btl):" | cut -c1-60
if [ "$LAYOUT" = 1node ]; then MAP="--map-by ppr:2:node"; else MAP="--map-by ppr:1:node"; fi
# startup time of an empty MPI program from this prefix
cat > $OUT/hello.c <<'C'
#include <mpi.h>
int main(int c,char**v){MPI_Init(&c,&v);MPI_Finalize();return 0;}
C
mpicc -o $OUT/hello $OUT/hello.c
T0=$(date +%s.%N); mpirun -n 2 $MAP --mca pml $PML $EXTRA $OUT/hello; echo "hello startup: $(echo "$(date +%s.%N) - $T0" | bc 2>/dev/null || python3 -c "import time;print(time.time()-$T0)") s"
mpirun -n 2 $MAP --tag-output --mca pml $PML $EXTRA $OUT/probe ompi.pml_ob1_ 1 > $OUT/probe.$PML.$LAYOUT.log 2>&1 || echo "probe exit $?"
wc -l $OUT/probe.$PML.$LAYOUT.log; echo OUT=$OUT
