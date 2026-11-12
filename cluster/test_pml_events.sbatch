#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --time=0:30:00
#SBATCH -J pml-ev
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# usage: sbatch test_pml_events.sbatch [name]   uses install/<name>-dbg and the dev tree
set -eo pipefail
NAME=${1:-dev}
P=/data/fdai0231/edumpi-t/install/$NAME-dbg
TREE=/data/fdai0231/edumpi-t/devtree/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
OUT=/data/fdai0231/edumpi-t/probe/pml-$SLURM_JOB_ID; mkdir -p $OUT
(cd $TREE && export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1) && touch ompi/test/t/Makefile.am && make -C ompi/test/t Makefile 2>&1 | tail -n 3)
make -C $TREE/ompi/test/t mpi_t_event_pml_p2p 2>&1 | tail -n 3
T=$TREE/ompi/test/t/mpi_t_event_pml_p2p
mpicc -O1 -o $OUT/probe $HOME/edumpi_t/probe/mpit_ob1_probe.c
for pml in ob1 ucx; do
  echo "=== test singleton pml $pml"; OMPI_MCA_pml=$pml $T || echo "TEST EXIT $?"
  echo "=== test mpirun -n1 pml $pml";  mpirun -n 1 --mca pml $pml $T || echo "TEST EXIT $?"
  echo "=== probe 2 nodes pml $pml"
  PROBE_SKIP=ob1 mpirun --map-by ppr:1:node --tag-output --mca pml $pml $OUT/probe ompi.pml_ 1 > $OUT/probe-$pml.txt 2>&1 || echo "PROBE EXIT $?"
  grep -c "^.*EV" $OUT/probe-$pml.txt || true
done
echo DONE
