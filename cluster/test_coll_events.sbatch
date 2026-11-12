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
run() { echo "=== $*"; mpirun --map-by ppr:2:node "$@" $T 2>&1 | grep -v "^\[" | sort | uniq | grep -E 'op (7|2|6) |PASS|FAIL|RESULT|SKIP' | head -n 60; }
run --mca coll ^han --mca pml ob1
run --mca coll ^han --mca pml ucx
run --mca coll ^han --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_bcast_algorithm 6 -x EXPECT_BCAST_ALGORITHM=6
run --mca coll ^han --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_bcast_algorithm 3 -x EXPECT_BCAST_ALGORITHM=3
echo "=== han (default priority) with forced tuned bcast on the helper communicators"
run --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_bcast_algorithm 6
echo "##### other components"
HOST1=$(scontrol show hostnames $SLURM_JOB_NODELIST | head -n1)
runl() { echo "=== local(2 ranks, $HOST1): $*"; mpirun --host $HOST1:2 -n 2 "$@" $T 2>&1 | grep -E 'component (1|5|6|7) |PASS|FAIL|RESULT|SKIP|rror' | sort -u | head -n 30; }
echo "--- basic only (component 1)";  run --mca coll basic,libnbc,self
echo "--- adapt (component 5)";       run --mca coll_adapt_priority 100 --mca coll ^han
echo "--- xhc (component 6, one node)";   runl --mca coll_xhc_priority 100 --mca coll ^han
echo "--- acoll (component 7, one node)"; runl --mca coll_acoll_priority 100 --mca coll_acoll_comm_size_thresh 2 --mca coll ^han
echo "=== singleton"; $T; echo "exit $?"
echo DONE
