#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --exclusive
#SBATCH --time=3:00:00
#SBATCH -J ompi-dev
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# Incremental debug build in a PERSISTENT tree on /data (BeeGFS), for development.
# usage: sbatch dev_build.sbatch <name>
# Sources come from ~/edumpi_t/src/code-ompi_edumpi_t (rsync keeps mtimes, so make rebuilds only what changed).
# First run: autogen + configure + full build (~40 min); later runs: minutes.
set -eo pipefail
NAME=${1:-dev}
SRC=$HOME/edumpi_t/src/code-ompi_edumpi_t
TREE=/data/fdai0231/edumpi-t/devtree/$NAME
PREFIX=/data/fdai0231/edumpi-t/install/$NAME-dbg
hostname; date
export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal
export M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1)
test -x "$M4"
mkdir -p $TREE
rsync -a --exclude=.git $SRC/ $TREE/
cd $TREE
if [ ! -f Makefile ]; then
  ./autogen.pl
  ./configure --prefix=$PREFIX --with-pmix=internal --with-hwloc=internal --with-prrte=internal \
              --disable-mpi-fortran --enable-debug
fi
make -j32 2>&1 | tail -n 40
make install 2>&1 | tail -n 3
git -C $SRC rev-parse HEAD > $PREFIX/EDUMPI_T_COMMIT
echo DEV_BUILD_OK; date
