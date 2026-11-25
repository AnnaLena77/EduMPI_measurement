#!/usr/bin/env bash
# Ruft Compose für die Dev-Umgebung auf – mit Docker oder Podman, je nachdem was da ist.
#   dev/compose.sh up -d --build
#   dev/compose.sh exec -u vscode dev bash
# Erzwingen: EDUMPI_ENGINE=docker|podman dev/compose.sh …
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
FILES=(-f "$DIR/compose.yaml")

# Gemeinsame Dev-Werte (Ports usw.); bereits gesetzte Variablen haben Vorrang
while IFS='=' read -r key value; do
    [[ "$key" =~ ^[A-Z_][A-Z0-9_]*$ ]] || continue
    [[ -n "${!key:-}" ]] || export "$key=$value"
done < "$DIR/gui.env"

use_docker() { docker info >/dev/null 2>&1; }
use_podman() { command -v podman >/dev/null 2>&1; }
# "docker" kann auch Podman sein (podman-docker, podman-mac-helper)
docker_is_podman() { docker version 2>/dev/null | grep -qi podman; }

engine="${EDUMPI_ENGINE:-}"
if [[ -z "$engine" ]]; then
    if use_docker; then engine=docker
    elif use_podman; then engine=podman
    else
        echo "Weder Docker (mit laufendem Daemon) noch Podman gefunden." >&2
        exit 1
    fi
fi

# Podman (rootless): Einstellungen für den dev-Service, siehe compose.podman.yaml
if [[ "$engine" == podman ]] || docker_is_podman; then
    FILES+=(-f "$DIR/compose.podman.yaml")
elif [[ "$engine" == docker ]]; then
    # Vor Docker 28 waren auf 127.0.0.1 veröffentlichte Ports aus dem LAN erreichbar –
    # mit dem öffentlichen Dev-Key wäre das ein offener Zugang zum Cluster
    v="$(docker version --format '{{.Server.Version}}' 2>/dev/null || true)"
    if [[ "${v%%.*}" =~ ^[0-9]+$ ]] && (( ${v%%.*} < 28 )); then
        echo "Warnung: Docker $v – ab 28.0 empfohlen (ältere Versionen machen 127.0.0.1-Ports im LAN erreichbar)" >&2
    fi
fi

# Zeitzone des Rechners an den Cluster weitergeben: die GUI vergleicht die Job-Endzeit aus
# scontrol mit ihrer lokalen Zeit
if [[ -z "${EDUMPI_TZ:-}" ]]; then
    EDUMPI_TZ="${TZ:-$(readlink /etc/localtime 2>/dev/null | sed 's#.*/zoneinfo/##')}"
    export EDUMPI_TZ="${EDUMPI_TZ:-UTC}"
fi

# Node-Größe passend zur Container-Engine (bei Docker Desktop/podman machine: deren VM).
# 4 Nodes teilen sich die CPUs; gerade Zahl, mindestens 2 (die GUI bindet je Rank 2 CPUs).
# Mehr Slurm-CPUs als echte Kerne würde mpirun --map-by :PE=2 scheitern lassen.
if [[ -z "${EDUMPI_NODE_CPUS:-}" || -z "${EDUMPI_NODE_MEM:-}" ]]; then
    if [[ "$engine" == podman ]]; then
        res="$(podman info --format '{{.Host.CPUs}} {{.Host.MemTotal}}' 2>/dev/null || true)"
    else
        res="$(docker info --format '{{.NCPU}} {{.MemTotal}}' 2>/dev/null || true)"
    fi
    read -r ncpu mem_bytes <<< "$res"
    if [[ "${ncpu:-}" =~ ^[0-9]+$ && "${mem_bytes:-}" =~ ^[0-9]+$ ]]; then
        c=$(( ncpu / 4 )); c=$(( c - c % 2 )); (( c < 2 )) && c=2
        m=$(( mem_bytes / 1024 / 1024 * 8 / 10 / 4 )); (( m < 1000 )) && m=1000
        export EDUMPI_NODE_CPUS="${EDUMPI_NODE_CPUS:-$c}" EDUMPI_NODE_MEM="${EDUMPI_NODE_MEM:-$m}"
    fi
fi
if [[ "${1:-}" == up ]]; then
    c="${EDUMPI_NODE_CPUS:-4}"
    echo "Dev-Cluster: 4 Nodes × $c CPUs, ${EDUMPI_NODE_MEM:-2000} MB → bis $(( 4 * c / 2 )) Ranks im EduMPI-Modus der GUI" >&2
fi

case "$engine" in
    docker)
        if docker compose version >/dev/null 2>&1; then
            exec docker compose "${FILES[@]}" "$@"
        fi
        exec docker-compose "${FILES[@]}" "$@"
        ;;
    podman)
        if command -v podman-compose >/dev/null 2>&1; then
            exec podman-compose "${FILES[@]}" "$@"
        fi
        exec podman compose "${FILES[@]}" "$@"
        ;;
    *)
        echo "Unbekannte Engine: $engine (docker oder podman)" >&2
        exit 1
        ;;
esac
