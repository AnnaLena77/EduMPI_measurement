#!/usr/bin/env bash
# Legt ein "GUI-Präfix" an: das Verzeichnis, das die EduMPI-GUI als Installationspfad des
# Clusters nutzt. Die GUI ruft nur <präfix>/bin/mpicc und <präfix>/bin/mpirun auf; hier sind das
# dünne Wrapper um ein echtes Open MPI, mpirun lädt libedumpi.so (PostgreSQL-Sink) nur in die
# Ranks. Wie cluster/make_gui_prefix.sh, aber ohne Cluster-Pfade und überschreibt ein
# vorhandenes Präfix (Dev-Container, Demo-Image).
#   dev/make-prefix.sh <ompi-präfix> <gui-präfix> <libedumpi.so>
set -euo pipefail
OMPI="${1:?ompi-präfix}"; GUI="${2:?gui-präfix}"; LIB="${3:?libedumpi.so}"
[[ -x "$OMPI/bin/mpicc" ]] || { echo "kein mpicc in $OMPI" >&2; exit 1; }
[[ -f "$LIB" ]] || { echo "fehlt: $LIB" >&2; exit 1; }
src="$(cd "$(dirname "$0")/.." && pwd)"

mkdir -p "$GUI/bin" "$GUI/lib"
install -m 0755 "$LIB" "$GUI/lib/libedumpi.so"

cat > "$GUI/bin/mpicc" <<W
#!/usr/bin/env bash
exec $OMPI/bin/mpicc "\$@"
W

cat > "$GUI/bin/mpirun" <<W
#!/usr/bin/env bash
# EduMPI-Messung: mpirun von $OMPI mit libedumpi.so in den Ranks (Datenbank-Sink).
# Die GUI exportiert DB_* und EDUMPI_PROGRAM, SLURM_JOBID setzt Slurm.
export PATH=$OMPI/bin:\$PATH LD_LIBRARY_PATH=$OMPI/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}
export PMIX_MCA_pcompress_base_silence_warning=1
export EDUMPI_SINK=\${EDUMPI_SINK:-pg}
# Die GUI schreibt "--map-by :PE=2" (leere Objektebene); die Laufzeit des MPI_T-Forks lehnt das
# auf Nodes ohne SMT ab, "slot:PE=2" geht und bindet je Rank zwei Kerne (siehe docs/STATUS.md)
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
# LD_PRELOAD nur für die Ranks, nicht für mpirun selbst
exec $OMPI/bin/mpirun -x LD_PRELOAD=$GUI/lib/libedumpi.so -x EDUMPI_SINK -x EDUMPI_PROGRAM \\
     -x DB_HOST -x DB_PORT -x DB_USER -x DB_NAME -x DB_PW -x SLURM_JOBID "\${args[@]}"
W
chmod 755 "$GUI/bin/mpicc" "$GUI/bin/mpirun"

{
    echo "built $(date -u +%FT%TZ)"
    echo "ompi prefix: $OMPI"
    [[ -f "$OMPI/OMPI_SOURCE" ]] && cat "$OMPI/OMPI_SOURCE"
    echo "tool: $(git -c safe.directory="$src" -C "$src" rev-parse HEAD 2>/dev/null || echo 'keine git-Info')"
} > "$GUI/VERSION"
echo "GUI-Präfix fertig: $GUI (Installationspfad in der GUI)"
