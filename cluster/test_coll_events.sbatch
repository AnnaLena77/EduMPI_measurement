#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --time=0:30:00
#SBATCH -J coll-ev
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# usage: sbatch test_coll_events.sbatch [name]   builds the test in the dev tree, runs it in several modes
set -o pipefail
NAME=${1:-dev}
P=/data/fdai0231/edumpi-t/install/$NAME-dbg
TREE=/data/fdai0231/edumpi-t/devtree/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal
export M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1)
(cd $TREE && touch ompi/test/t/Makefile.am && make -C ompi/test/t Makefile 2>&1 | tail -n 1)
make -C $TREE/ompi/test/t mpi_t_event_coll_algorithm 2>&1 | tail -n 3
T=$TREE/ompi/test/t/mpi_t_event_coll_algorithm
run() { echo "=== $*"; mpirun --map-by ppr:2:node "$@" $T 2>&1 | grep -v "^\[" | sort | uniq | grep -E 'op (7|2) |PASS|FAIL|RESULT|SKIP' | head -n 60; }
run --mca coll ^han --mca pml ob1
run --mca coll ^han --mca pml ucx
run --mca coll ^han --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_bcast_algorithm 6 -x EXPECT_BCAST_ALGORITHM=6
run --mca coll ^han --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_bcast_algorithm 3 -x EXPECT_BCAST_ALGORITHM=3
echo "=== han (default priority) with forced tuned bcast on the helper communicators"
run --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_bcast_algorithm 6
echo "=== singleton"; $T; echo "exit $?"
echo DONE
