#!/usr/bin/env bash
# Baut das Open MPI, gegen das die Messbibliothek läuft, und installiert es nach $OMPI_PREFIX
# (im Dev-Container /opt/ompi, ein Volume, das login und node1..4 mitbenutzen).
# Standard ist der Fork FuldaHPC/code-ompi_edumpi_t mit den MPI_T-Events, die das Tool liest
# (README: mpit/pml-common-events und mpit/coll-events; die Branches bauen aufeinander auf).
# Mit einem Open MPI ohne diese Events misst das Tool nur über PMPI.
#   dev/build-ompi.sh            # Quellen holen/aktualisieren, inkrementell bauen
#   dev/build-ompi.sh --clean    # autogen + configure neu
#   dev/build-ompi.sh --debug    # mit --enable-debug (langsamer, für Arbeit am Fork)
#   OMPI_REF=mpit/coll-events dev/build-ompi.sh --clean   # anderer Stand
# Dauert beim ersten Mal je nach Rechner 15–45 Minuten; Quellen liegen in $OMPI_PREFIX/src
# (OMPI_SRC). OMPI_CONFIGURE_EXTRA hängt configure-Optionen an (z. B. das Demo-Image).
set -euo pipefail

OMPI_REPO="${OMPI_REPO:-https://github.com/FuldaHPC/code-ompi_edumpi_t}"
OMPI_REF="${OMPI_REF:-mpit/request-events-pvars}"
PREFIX="${OMPI_PREFIX:-/opt/ompi}"
SRC="${OMPI_SRC:-$PREFIX/src}"

clean=0 debug=0
for a in "$@"; do
    case "$a" in
        --clean) clean=1 ;;
        --debug) debug=1; clean=1 ;;
        *) echo "Unbekannte Option: $a (--clean, --debug)" >&2; exit 2 ;;
    esac
done

# Quellen: flach per Ref (Branch, Tag oder Commit), Submodule (openpmix, prrte, oac) ebenso
if [[ ! -d "$SRC/.git" ]]; then
    git init -q "$SRC"
    git -C "$SRC" remote add origin "$OMPI_REPO"
fi
git -C "$SRC" remote set-url origin "$OMPI_REPO"
git -C "$SRC" fetch -q --depth 1 origin "$OMPI_REF"
old="$(git -C "$SRC" rev-parse -q --verify HEAD || true)"
git -C "$SRC" checkout -q --force FETCH_HEAD
git -C "$SRC" submodule update -q --init --recursive --depth 1
new="$(git -C "$SRC" rev-parse HEAD)"
[[ "$old" != "$new" ]] && echo "Open MPI: $OMPI_REPO $OMPI_REF → ${new:0:10}"

cd "$SRC"
if (( clean )) || [[ ! -f Makefile ]]; then
    [[ -f Makefile ]] && make distclean >/dev/null 2>&1 || true
    ./autogen.pl
    extra=()
    (( debug )) && extra+=(--enable-debug)
    # shellcheck disable=SC2206  # absichtlich an Leerzeichen getrennt
    extra+=(${OMPI_CONFIGURE_EXTRA:-})
    command -v ccache >/dev/null && extra+=(CC="ccache gcc" CXX="ccache g++")
    ./configure --prefix="$PREFIX" \
        --with-pmix=internal --with-hwloc=internal --with-prrte=internal \
        --disable-mpi-fortran --disable-oshmem --disable-sphinx \
        "${extra[@]}"
fi

make -j"$(nproc)"
make install
{ echo "repo $OMPI_REPO"; echo "ref $OMPI_REF"; echo "commit $new"; } > "$PREFIX/OMPI_SOURCE"
echo "Fertig: Open MPI in $PREFIX (${new:0:10}); als Nächstes dev/build-edumpi.sh"
