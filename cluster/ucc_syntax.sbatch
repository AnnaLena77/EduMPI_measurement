#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --time=0:15:00
#SBATCH -J ucc-syn
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# syntax check (gcc -fsyntax-only) of the coll/ucc sources, which are not part of the cluster build (no --with-ucc)
TREE=/data/fdai0231/edumpi-t/devtree/${1:-dev}
UCCINC=/data/fdai0231/edumpi-t/ucc-include  # copied from the -dev output of ucc-1.7.0 (the Nix store path is not on the compute nodes)
ls $UCCINC/ucc/api/ucc.h
cd $TREE/ompi/mca/coll/basic
V() { echo "p-%: ; @echo \$(\$*)" | make -s -f Makefile -f - p-$1; }
FLAGS="$(V DEFS) $(V DEFAULT_INCLUDES) $(V AM_CPPFLAGS) $(V CPPFLAGS) $(V AM_CFLAGS) $(V CFLAGS)"
echo "flags: $FLAGS" | cut -c1-400
cd ../ucc
for f in coll_ucc_*.c; do
  echo "== $f"
  gcc $FLAGS -I$UCCINC -I. -I$TREE/ompi/mca/coll/ucc -fsyntax-only $f 2>&1 | grep -E "error|warning" | head -n 5
done
