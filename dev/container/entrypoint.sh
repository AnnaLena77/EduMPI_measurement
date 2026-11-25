#!/usr/bin/env bash
# Startet je nach EDUMPI_ROLE die Dienste eines Containers (ohne systemd):
#   dev    munge, DB-Weiterleitung, dann CMD (Build-Container für VS Code)
#   login  munge, DB-Weiterleitung, sshd, slurmctld
#   node   munge, DB-Weiterleitung, slurmd
set -euo pipefail

role="${EDUMPI_ROLE:-dev}"

# slurm.conf aus der Vorlage: Größe der Nodes passend zum Rechner (in allen Containern gleich)
node_cpus="${EDUMPI_NODE_CPUS:-4}"
node_mem="${EDUMPI_NODE_MEM:-2000}"
sed -e "s/@NODE_CPUS@/$node_cpus/" -e "s/@NODE_MEM@/$node_mem/" \
    /etc/edumpi/slurm.conf.in > /etc/slurm/slurm.conf

# munge: gemeinsamer Key aus dem Image, munged läuft als Nutzer munge
install -o munge -g munge -m 0400 /etc/edumpi/munge.key /etc/munge/munge.key
install -d -o munge -g munge -m 0755 /run/munge
runuser -u munge -- /usr/sbin/munged --force
for _ in $(seq 50); do [[ -S /run/munge/munge.socket.2 ]] && break; sleep 0.1; done
munge -n | unmunge >/dev/null

# Die GUI gibt ihren DB-Host und -Port (127.0.0.1:$EDUMPI_DB_PORT) an die MPI-Ranks weiter;
# damit dieselben Werte auch in den Containern funktionieren, zeigt dieser Port hier auf die DB
db_port="${EDUMPI_DB_PORT:-5432}"
socat "TCP-LISTEN:$db_port,bind=127.0.0.1,fork,reuseaddr" TCP:db:5432 &

# Zeitzone für SSH-Sitzungen und damit für Slurm-Jobs (pam_env liest /etc/environment).
# PGTZ: EduMPI schreibt Start-/Endzeit als lokale Zeit ohne Offset in TIMESTAMPTZ-Spalten
tz="${TZ:-UTC}"
[[ -e "/usr/share/zoneinfo/$tz" ]] || tz=UTC
sed -i -e '/^TZ=/d' -e '/^PGTZ=/d' /etc/environment
printf 'TZ="%s"\nPGTZ="%s"\n' "$tz" "$tz" >> /etc/environment
ln -sf "/usr/share/zoneinfo/$tz" /etc/localtime
echo "$tz" > /etc/timezone

case "$role" in
    login)
        # Slurm-Zustand liegt im Volume; Job-IDs (= EduMPI-Run-IDs) laufen so weiter
        chown -R slurm:slurm /var/lib/slurm/slurmctld
        install -d -m 0755 /run/sshd
        /usr/sbin/sshd -e
        exec /usr/sbin/slurmctld -D
        ;;
    node)
        exec /usr/sbin/slurmd -D -N "$(hostname -s)"
        ;;
    dev)
        exec "$@"
        ;;
    *)
        echo "Unbekannte EDUMPI_ROLE: $role (dev, login, node)" >&2
        exit 1
        ;;
esac
