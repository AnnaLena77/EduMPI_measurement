#!/usr/bin/env bash
# Builds a "GUI prefix": a directory that EduMPI_GUI accepts as "cluster EduMPI path". The GUI only calls
# <path>/bin/mpicc and <path>/bin/mpirun, so these are thin wrappers around a real Open MPI prefix; mpirun preloads
# libedumpi.so (PostgreSQL sink) into the application ranks only.
# usage (run as a job, build on /data):  make_gui_prefix.sh <ompi-prefix> <new-gui-prefix> <tool-source-dir>
# The new prefix must not exist (never install into an existing prefix such as ~/EduMPI_Testumgebung).
set -e
OMPI=${1:?ompi prefix}; GUI=${2:?new gui prefix}; SRC=${3:?tool source dir}
[ -e "$GUI" ] && { echo "refusing: $GUI exists"; exit 1; }
[ -x "$OMPI/bin/mpicc" ] || { echo "no mpicc in $OMPI"; exit 1; }
PQLIB=$(dirname "$(readlink -f /etc/profiles/per-group/cluster/lib/libpq.so)")
PQINC=/data/fdai0231/edumpi-t/pq-include
W=$GUI.build; rm -rf "$W"; mkdir -p "$W"; cp -r "$SRC"/. "$W"/
export PATH=$OMPI/bin:$PATH OPAL_PREFIX=$OMPI LD_LIBRARY_PATH=$OMPI/lib:$LD_LIBRARY_PATH
(cd "$W" && make clean >/dev/null && make CC=mpicc PG=1 PQINC=$PQINC PQLIB=$PQLIB libedumpi.so)
test -f "$W/libedumpi.so"
mkdir -p "$GUI/bin" "$GUI/lib"
cp "$W/libedumpi.so" "$GUI/lib/"
cat > "$GUI/bin/mpicc" <<EOF
#!/usr/bin/env bash
export OPAL_PREFIX=$OMPI
exec $OMPI/bin/mpicc "\$@"
EOF
cat > "$GUI/bin/mpirun" <<EOF
#!/usr/bin/env bash
# EduMPI_T backend: database sink. The GUI exports DB_* and EDUMPI_PROGRAM (and SLURM_JOBID is set by Slurm).
export OPAL_PREFIX=$OMPI
export PATH=$OMPI/bin:\$PATH LD_LIBRARY_PATH=$OMPI/lib:\$LD_LIBRARY_PATH PMIX_MCA_pcompress_base_silence_warning=1
export EDUMPI_SINK=\${EDUMPI_SINK:-pg}
# EduMPI_GUI writes "--map-by :PE=2" (empty object level). The runtime of the MPI_T fork rejects that on nodes without SMT
# ("multiple cpus-per-proc ... BYCORE", job 10520); "slot:PE=2" is accepted and binds each rank to two cores.
args=()
prev=
for a in "\$@"; do
    case "\$prev:\$a" in
        --map-by::PE=*) a="slot\$a" ;;
    esac
    case "\$a" in
        --map-by=:PE=*) a="--map-by=slot\${a#--map-by=}" ;;
    esac
    args+=("\$a"); prev=\$a
done
# the preload goes to the application ranks only, not to mpirun itself
exec $OMPI/bin/mpirun -x LD_PRELOAD=$GUI/lib/libedumpi.so -x EDUMPI_SINK -x EDUMPI_PROGRAM \\
     -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x SLURM_JOBID "\${args[@]}"
EOF
chmod +x "$GUI/bin/mpicc" "$GUI/bin/mpirun"
{ echo "built $(date -u +%FT%TZ)"; echo "ompi prefix: $OMPI"; (cd "$SRC" && git rev-parse HEAD 2>/dev/null || echo "tool: no git info"); } > "$GUI/VERSION"
rm -rf "$W"
echo "GUI prefix ready: $GUI"
