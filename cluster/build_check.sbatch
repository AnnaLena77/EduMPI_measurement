#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --exclusive
#SBATCH --time=3:00:00
#SBATCH -J ompi-bcheck
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# usage: sbatch build_check.sbatch <name>   debug build + install + MPI_T unit tests (test/util, ompi/test/t)
set -eo pipefail
NAME=${1:?name}; KIND=dbg
SRC=$HOME/edumpi_t/src/code-ompi_edumpi_t
PREFIX=/data/fdai0231/edumpi-t/install/$NAME-$KIND
hostname; date
export ACLOCAL_PATH=$(dirname $(dirname $(readlink -f $(which libtoolize))))/share/aclocal
export M4=$(grep -o "/nix/store/[a-z0-9]*-gnum4-[0-9.]*/bin/m4" $(readlink -f $(which autom4te)) | head -1)
BUILD=/tmp/$USER-edumpi-t-$SLURM_JOB_ID
trap "rm -rf $BUILD" EXIT
mkdir -p $BUILD; rsync -a --exclude=.git $SRC/ $BUILD/; cd $BUILD
./autogen.pl
./configure --prefix=$PREFIX --with-pmix=internal --with-hwloc=internal --with-prrte=internal --disable-mpi-fortran --enable-debug
make -j32
make install
git -C $SRC rev-parse HEAD > $PREFIX/EDUMPI_T_COMMIT; ./config.status --config > $PREFIX/EDUMPI_T_CONFIGURE
echo INSTALL_OK; date
export PATH=$PREFIX/bin:$PATH OPAL_PREFIX=$PREFIX LD_LIBRARY_PATH=$PREFIX/lib PMIX_MCA_pcompress_base_silence_warning=1
set +e
make -C test/support 2>&1 | tail -n 3
make -C test/util check 2>&1 | tail -n 60
make -C ompi/test/t check 2>&1 | tail -n 80
echo CHECK_DONE; date
