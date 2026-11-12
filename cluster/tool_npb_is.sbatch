#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 2
#SBATCH --ntasks-per-node=2
#SBATCH --exclusive
#SBATCH --time=1:00:00
#SBATCH -J tool-npb
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
# NPB IS (class C by default, 4 ranks on 2 nodes, 2 cores per rank) without tool, PMPI only, MPI_T (default events); ucx and ob1.
# usage: sbatch tool_npb_is.sbatch [mpi-install-name] [class]
NAME=${1:-dev-rel}; CLASS=${2:-C}
P=/data/fdai0231/edumpi-t/install/$NAME
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
W=/data/fdai0231/edumpi-t/toolrun/npb-$SLURM_JOB_ID; mkdir -p $W; cp -r $HOME/edumpi_t/tool/. $W/; cd $W; make clean >/dev/null
make CC=mpicc libedumpi.so 2>&1 | grep -E "error" | head
test -f libedumpi.so || { echo BUILD FAILED; exit 1; }
# NPB IS sources: copy only what is needed
S=$HOME/benchmarks/NPB3.4.3/NPB3.4-MPI; N=$W/npb; mkdir -p $N; cp -r $S/IS $S/common $S/config $S/sys $S/Makefile $N/ 2>/dev/null
cat > $N/config/make.def <<EOF
MPIFC = mpif90
FLINK = \$(MPIFC)
FMPI_LIB =
FMPI_INC =
FFLAGS = -O3
FLINKFLAGS = \$(FFLAGS)
MPICC = mpicc
CLINK = \$(MPICC)
CMPI_LIB =
CMPI_INC =
CFLAGS = -O3
CLINKFLAGS = \$(CFLAGS)
CC = gcc -g
BINDIR = ../bin
CONVERTFLAG = -DFORTRAN_REC_SIZE=0
RAND = randi8
EOF
rm -f $N/IS/*.o $N/IS/npbparams.h $N/common/*.o; mkdir -p $N/bin; (cd $N && make IS NPROCS=4 CLASS=$CLASS 2>&1 | tail -n 8)
EXE=$N/bin/is.$CLASS.x; test -x $EXE || { echo NPB BUILD FAILED; exit 1; }
mkdir -p run && cd run
BIND="--map-by ppr:2:node:PE=2 --bind-to core"
for pml in ucx ob1; do
  for mode in none pmpi mpit; do
    for rep in 1 2 3; do
      case $mode in
        none) ENVS="" ;;
        pmpi) ENVS="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_MPIT=0 -x EDUMPI_LOG_DIR=." ;;
        mpit) ENVS="-x LD_PRELOAD=$W/libedumpi.so -x EDUMPI_LOG_DIR=." ;;
      esac
      echo -n "$pml $mode run$rep: "
      mpirun $BIND --mca pml $pml $ENVS $EXE 2>&1 | grep -E "Time in seconds|Verification|Mop/s total" | tr -s ' ' | tr '\n' ' '; echo
    done
  done
done
ls -l edumpi.0.bin | awk '{print "log file of rank 0 (last run): " $5 " bytes"}'
