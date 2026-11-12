#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --time=0:20:00
#SBATCH -J req-ev
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# builds and runs mpi_t_event_request_state (2 nodes x 2 ranks) on the release dev tree; usage: sbatch test_request_events.sbatch
P=/data/fdai0231/edumpi-t/install/dev-rel
TREE=/data/fdai0231/edumpi-t/devtree/dev-rel
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal
export M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1)
(cd $TREE && touch ompi/test/t/Makefile.am && make -C ompi/test/t Makefile 2>&1 | tail -n 1)
make -C $TREE/ompi/test/t mpi_t_event_request_state 2>&1 | tail -n 2
make -C $TREE/ompi/test/t mpi_t_event_request_handle 2>&1 | tail -n 1
T=$TREE/ompi/test/t/mpi_t_event_request_state
TH=$TREE/ompi/test/t/mpi_t_event_request_handle
for pml in ob1 ucx; do
  echo "=== request state test, pml $pml"; mpirun --map-by ppr:2:node --mca pml $pml $T 2>&1 | grep -E "PASS|FAIL|RESULT|INFO|SKIP" | sort | uniq -c | sort -rn | head -n 14
  echo "=== request handle (lifetime) test, pml $pml"; mpirun --map-by ppr:2:node --mca pml $pml $TH 200000 2>&1 | grep -E "PASS|FAIL|RESULT|INFO|SKIP" | sort | uniq -c | sort -rn | head -n 14
done
echo "=== singleton lifetime test"; $TH 200000; echo "exit $?"
echo DONE
