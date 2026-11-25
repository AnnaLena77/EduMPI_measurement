#!/usr/bin/env bash
# Baut Score-P und Scalasca gegen das Open MPI aus dev/build-ompi.sh und installiert sie ins Volume
# /opt/perftools (im Image verlinkt als /opt/scorep und /opt/scalasca, wie die GUI es
# im Score-P-Modus erwartet). Einmalig nötig, dauert je nach Rechner 10–30 Minuten;
# danach bleibt es im Volume, auch über Image-Neubauten hinweg.
#   dev/build-scorep.sh          # nur bauen, wenn noch nicht installiert
#   dev/build-scorep.sh --clean  # neu bauen (z. B. nach Versionswechsel)
# Ein Neubau der Messbibliothek erfordert keinen Neubau von Score-P; nach einem Wechsel des
# Open-MPI-Stands (dev/build-ompi.sh) mit --clean neu bauen.
set -euo pipefail

SCOREP_VERSION=10.1
SCOREP_SHA256=39edf6790c4af25679ab46e361091f177d40ad8f8cfff1b4bf81e775fc59f0c1
SCALASCA_VERSION=2.6.2
SCALASCA_SHA256=17e72fd908be43879955e4ed49c2732d4dbda7d295fec2d8b3af7ddafe1202a0
URL=https://perftools.pages.jsc.fz-juelich.de/cicd

PREFIX=/opt/perftools
SRC="$PREFIX/src"
BUILD="${TMPDIR:-/tmp}/perftools-build"

if [[ "${1:-}" != "--clean" && -x "$PREFIX/scorep/bin/scorep" && -x "$PREFIX/scalasca/bin/square" ]]; then
    echo "Score-P und Scalasca sind schon installiert ($PREFIX); neu bauen mit --clean"
    exit 0
fi
OMPI="${OMPI_PREFIX:-/opt/ompi}"
if [[ ! -x "$OMPI/bin/mpicc" ]]; then
    echo "Open MPI fehlt unter $OMPI – zuerst dev/build-ompi.sh ausführen" >&2
    exit 1
fi
export PATH="$OMPI/bin:$PATH"

fetch() {  # name version sha256
    local file="$1-$2.tar.gz"
    mkdir -p "$SRC"
    if [[ ! -f "$SRC/$file" ]]; then
        curl -fL --retry 3 -o "$SRC/$file.part" "$URL/$1/tags/$1-$2/$file"
        mv "$SRC/$file.part" "$SRC/$file"
    fi
    echo "$3  $SRC/$file" | sha256sum -c --quiet
    tar xzf "$SRC/$file" -C "$BUILD"
}

rm -rf "$BUILD" "$PREFIX/scorep" "$PREFIX/scalasca"
mkdir -p "$BUILD"

# Score-P bringt OTF2, CubeW, CubeLib und OPARI2 mit; Scalasca nutzt danach dieselben
fetch scorep "$SCOREP_VERSION" "$SCOREP_SHA256"
mkdir "$BUILD/scorep-build" && cd "$BUILD/scorep-build"
"../scorep-$SCOREP_VERSION/configure" --prefix="$PREFIX/scorep" \
    --with-mpi=openmpi --without-shmem --enable-gcc-plugin
make -j"$(nproc)"
make install

fetch scalasca "$SCALASCA_VERSION" "$SCALASCA_SHA256"
mkdir "$BUILD/scalasca-build" && cd "$BUILD/scalasca-build"
"../scalasca-$SCALASCA_VERSION/configure" --prefix="$PREFIX/scalasca" \
    --with-mpi=openmpi \
    --with-cubew="$PREFIX/scorep/bin" --with-otf2="$PREFIX/scorep/bin"
make -j"$(nproc)"
make install

rm -rf "$BUILD"
echo "Fertig: $PREFIX/scorep, $PREFIX/scalasca"
