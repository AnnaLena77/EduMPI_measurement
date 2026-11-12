#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --exclusive
#SBATCH --time=3:00:00
#SBATCH -J ompi-build
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# usage: sbatch build.sbatch <name> <rel|dbg>   (builds the working tree of ~/edumpi_t/src/code-ompi_edumpi_t)
set -eo pipefail
NAME=${1:?name}; KIND=${2:-rel}
SRC=$HOME/edumpi_t/src/code-ompi_edumpi_t
PREFIX=/data/fdai0231/edumpi-t/install/$NAME-$KIND
hostname; date
export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal
export M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1)
test -x "$M4"
BUILD=/tmp/$USER-edumpi-t-$SLURM_JOB_ID
trap "rm -rf $BUILD" EXIT
mkdir -p $BUILD
rsync -a --exclude=.git $SRC/ $BUILD/
cd $BUILD
./autogen.pl
EXTRA=""; [ "$KIND" = dbg ] && EXTRA="--enable-debug"
./configure --prefix=$PREFIX --with-pmix=internal --with-hwloc=internal --with-prrte=internal \
            --disable-mpi-fortran $EXTRA
make -j32
make install
git -C $SRC rev-parse HEAD > $PREFIX/EDUMPI_T_COMMIT
./config.status --config > $PREFIX/EDUMPI_T_CONFIGURE
echo BUILD_AND_INSTALL_OK; date
