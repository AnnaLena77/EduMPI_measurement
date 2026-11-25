#!/usr/bin/env bash
# Baut die Messbibliothek libedumpi.so (mit PostgreSQL-Sink) gegen das Open MPI aus
# dev/build-ompi.sh und legt in $EDUMPI_PREFIX (im Dev-Container /opt/edumpi, ein Volume, das
# login und node1..4 mitbenutzen) das Verzeichnis an, das die GUI als "Installationspfad"
# erwartet: bin/mpicc und bin/mpirun (mpirun lädt libedumpi.so per LD_PRELOAD in die Ranks).
# bear erzeugt dabei compile_commands.json für clangd.
#   dev/build-edumpi.sh          # inkrementell
#   dev/build-edumpi.sh --clean  # make clean vorher
set -euo pipefail
cd "$(dirname "$0")/.."

OMPI="${OMPI_PREFIX:-/opt/ompi}"
PREFIX="${EDUMPI_PREFIX:-/opt/edumpi}"
if [[ ! -x "$OMPI/bin/mpicc" ]]; then
    echo "Open MPI fehlt unter $OMPI – zuerst dev/build-ompi.sh ausführen" >&2
    exit 1
fi
export PATH="$OMPI/bin:$PATH"

[[ "${1:-}" == "--clean" ]] && make clean

bear -- make CC="ccache $OMPI/bin/mpicc" PG=1 \
    PQINC="$(pg_config --includedir)" PQLIB="$(pg_config --libdir)" libedumpi.so
dev/make-prefix.sh "$OMPI" "$PREFIX" libedumpi.so
