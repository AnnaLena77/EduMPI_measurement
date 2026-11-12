#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --time=0:40:00
#SBATCH -J regress
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# usage: sbatch regress_check.sbatch [name]: ompi/test/t and test/util "make check" in the dev tree (needs the dev build)
TREE=/data/fdai0231/edumpi-t/devtree/${1:-dev}
export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal
export M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1)
export PMIX_MCA_pcompress_base_silence_warning=1
cd $TREE
touch ompi/test/t/Makefile.am; make -C ompi/test/t Makefile >/dev/null 2>&1
make -C test/support libsupport.a 2>&1 | tail -n 1
for d in ompi/test/t test/util; do echo "=== $d"; make -C $d check 2>&1 | grep -E "^(PASS|FAIL|SKIP|XFAIL|ERROR)|^# (TOTAL|PASS|SKIP|FAIL|ERROR)" ; done
echo DONE
