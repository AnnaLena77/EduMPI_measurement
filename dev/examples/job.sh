#!/usr/bin/env bash
# Slurm-Job wie ihn die EduMPI-GUI erzeugt (Modus "EduMPI"), zum Testen ohne GUI.
# Im Login-Container als Nutzer dev, aus einem Verzeichnis unter /home/dev:
#   cp /workspaces/… oder per scp: edumpi_test.c und job.sh nach ~/eduMPI_files/
#   cd ~/eduMPI_files && sbatch job.sh
#SBATCH --partition=all
#SBATCH --ntasks=8
#SBATCH --job-name=eduMPI
#SBATCH --cpus-per-task=2
#SBATCH --exclusive

# Dieselben Werte, die man in der GUI einträgt (127.0.0.1 zeigt in jedem Container auf die DB)
export DB_HOST=127.0.0.1 DB_PORT=5432 DB_NAME=edumpi DB_USER=edumpi DB_PW=edumpi
export OMPI_MCA_coll_han_priority=0
export EDUMPI_PROGRAM=edumpi_test.c

/opt/edumpi/bin/mpicc edumpi_test.c -o edumpi_test -lm
/opt/edumpi/bin/mpirun -n "$SLURM_NTASKS" --map-by :PE=2 --mca pml ob1 ./edumpi_test ${EDUMPI_TEST_SECONDS:-60} ${EDUMPI_TEST_PHASE:-5}
