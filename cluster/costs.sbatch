#!/usr/bin/env bash
#SBATCH -p vip
#SBATCH -N 1
#SBATCH --ntasks-per-node=1
#SBATCH --exclusive
#SBATCH --time=0:10:00
#SBATCH -J costs
#SBATCH -o /home/fdai0231/edumpi_t/jobs/%x-%j.out
P=/data/fdai0231/edumpi-t/install/dev-rel
export PATH=$P/bin:$PATH OPAL_PREFIX=$P LD_LIBRARY_PATH=$P/lib:$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
cat /sys/devices/system/clocksource/clocksource0/current_clocksource
mpicc -O2 -o /tmp/costs-$$ $HOME/edumpi_t/tool/tests/costs.c && mpirun -n 1 /tmp/costs-$$
