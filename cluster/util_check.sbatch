#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --time=0:30:00
#SBATCH -J util-chk
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
set -o pipefail
TREE=/data/fdai0231/edumpi-t/devtree/${1:-dev}
export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal
export M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1)
cd $TREE
grep -n "libsupport" test/util/Makefile.am test/support/Makefile.am | head
make -C test/support libsupport.a 2>&1 | tail -n 5
ls -l test/support/libsupport.a
make -C test/util check 2>&1 | tail -n 40
