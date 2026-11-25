# dev/ – lokale Dev-Umgebung für EduMPI_measurement

Simulierter Slurm-Cluster mit Open MPI, der Messbibliothek (`libedumpi.so`) und TimescaleDB in Containern,
zum Entwickeln und Testen von Messung
und GUI auf dem eigenen Rechner. Ergänzt die native Installation, ersetzt sie nicht; Zeiten aus dem
Container taugen nicht für Performance-Aussagen.

| | |
| --- | --- |
| Engine | Docker ≥ 28.0 oder rootless Podman ≥ 4.3; Linux und macOS (auch Apple Silicon) |
| Start | immer über `dev/compose.sh` (wählt Engine und Compose-Dateien), nicht direkt `docker compose` |
| Nutzer / Partition | `dev` / `all` (wie von der GUI erwartet) |

## Aufbau

| Service | Rolle |
| --- | --- |
| `dev` | Build-Container (Autotools, gcc, libpq, bear, clangd); Repo unter `/workspaces/EduMPI_measurement`; VS Code hängt sich hier an |
| `login` | Slurm-Controller + SSH (`127.0.0.1:2222`) |
| `node1`…`node4` | Slurm-Rechenknoten; Größe passend zum Rechner |
| `db` | TimescaleDB mit `dev/edustore_schema.sql` (Kopie aus EduMPI `EduStore/`, `127.0.0.1:5432`) |

Alle außer `db` nutzen ein Image. Gemeinsame Volumes wie auf einem echten Cluster, alle im `dev`-Container gebaut:

| Pfad | Inhalt |
| --- | --- |
| `/opt/ompi` | Open MPI mit MPI_T-Events (`FuldaHPC/code-ompi_edumpi_t`), im `PATH` aller Container |
| `/opt/edumpi` | GUI-Präfix: `lib/libedumpi.so` und die Wrapper `bin/mpicc`, `bin/mpirun` (lädt die Messung per `LD_PRELOAD`) |
| `/opt/perftools` | optional Score-P/Scalasca |
| `/home/dev` | Home des Job-Nutzers |

## Schnellstart

```sh
dev/compose.sh up -d --build                             # Image bauen, Cluster starten
dev/compose.sh exec login sinfo                          # 4 Nodes "idle"?
dev/compose.sh exec -u vscode dev dev/build-ompi.sh      # Open MPI → /opt/ompi (einmalig, dauert)
dev/compose.sh exec -u vscode dev dev/build-edumpi.sh    # libedumpi.so + Wrapper → /opt/edumpi (Sekunden)
```

> **Wie viele Prozesse gehen?** Steht in der **allerersten Zeile** von `dev/compose.sh up`, vor den
> Build-Meldungen, die sie leicht nach oben schieben:
>
> ```text
> Dev-Cluster: 4 Nodes × 4 CPUs, 12748 MB → bis 8 Ranks im EduMPI-Modus der GUI
> ```
>
> „bis N“ = Höchstwert für „Number of Processes“ in der GUI; mehr bleibt dauerhaft PENDING.
> Später: `dev/compose.sh exec login sinfo -N -o '%N %c'` → Summe der CPUs ÷ 2.

`build-ompi.sh` holt den Fork flach (Standard `OMPI_REF=mpit/request-events-pvars`, der oberste getestete
Stand der aufeinander aufbauenden `mpit/*`-Branches) nach `/opt/ompi/src` und baut ihn als Release;
`--debug` für `--enable-debug`, `--clean` nach einem Wechsel von `OMPI_REF`. Mit einem anderen Open MPI
(`OMPI_REPO`, z. B. upstream) misst das Tool nur über PMPI.

`build-edumpi.sh` baut `libedumpi.so` mit PostgreSQL-Sink (`make PG=1`), erzeugt `compile_commands.json`
für clangd und legt mit `make-prefix.sh` das GUI-Präfix `/opt/edumpi` an (wie `cluster/make_gui_prefix.sh`,
ohne Cluster-Pfade). Nach Code-Änderungen erneut ausführen – alle Nodes sehen den neuen Stand sofort.

Nur für den Score-P-Modus der GUI, einmalig nach `build-ompi.sh`:

```sh
dev/compose.sh exec -u vscode dev dev/build-scorep.sh    # Score-P + Scalasca → /opt/perftools (dauert)
```

Lädt feste Versionen (Prüfsumme im Skript) und baut sie gegen `/opt/ompi`; im Image verlinkt als
`/opt/scorep` und `/opt/scalasca`, wo die GUI sie erwartet. Bleibt im Volume, auch über Image-Neubauten
und Neubauten der Messbibliothek hinweg; nach einem neuen Open-MPI-Stand mit `--clean`.

## Einstellungen

Standardwerte stehen in `dev/gui.env` – eine Quelle für `compose.sh`, `activate` und den Dev-Modus der
GUI. Gesetzte Umgebungsvariablen haben Vorrang (z. B. `EDUMPI_DB_PORT=5433 dev/compose.sh up -d`).
Nach Änderungen `dev/compose.sh up -d`.

| Variable | Standard | Wofür |
| --- | --- | --- |
| `EDUMPI_NODE_CPUS` | Kerne der Engine ÷ 4, gerade, ≥ 2 | CPUs je Node; Ranks ≤ 4 × CPUs ÷ 2 (die GUI fordert 2 je Rank an). Nicht über die echten Kerne setzen, sonst scheitert `mpirun --map-by :PE=2` |
| `EDUMPI_NODE_MEM` | 80 % RAM der Engine ÷ 4 | RAM je Node in MB |
| `EDUMPI_DEV_SSH_PORT` | `2222` (`gui.env`) | SSH-Port auf dem Rechner; bei `compose.sh` und `activate` gleich setzen |
| `EDUMPI_DB_PORT` | `5432` (`gui.env`) | DB-Port auf dem Rechner (z. B. `5433` neben lokalem PostgreSQL); in der GUI denselben eintragen |
| `EDUMPI_TZ` | Zeitzone des Rechners | Zeitzone im Cluster (die GUI vergleicht Job-Endzeiten mit ihrer Ortszeit) |
| `EDUMPI_ENGINE` | automatisch | `docker` oder `podman` erzwingen |

„Engine“ = Docker/Podman bzw. deren VM bei Docker Desktop/podman machine.

| Plattform | Besonderheit |
| --- | --- |
| Podman | `compose.sh` lädt `compose.podman.yaml` (eigener Nutzer = `vscode` im `dev`-Container, damit der Build ins Repo schreibt). VS Code: `dev.containers.dockerPath` = `podman`, `compose.podman.yaml` in `dockerComposeFile` ergänzen |
| SELinux | Repo und Schema werden mit `label=disable` eingebunden, ohne Umlabeln auf dem Host |

## SSH-Zugang

`dev/ssh/` enthält einen **festen, unsicheren Dev-Key** (ohne Passphrase, absichtlich öffentlich – nur für
diesen lokalen Container) und feste Host-Keys. `~/.ssh` bleibt unverändert.

```sh
source dev/ssh/activate      # nur diese Shell
ssh dev@edumpi-dev sinfo
edumpi_dev_deactivate        # oder Shell schließen
```

`activate` stellt `dev/ssh/bin` vorne in den `PATH`. Die Wrapper `ssh`/`scp` leiten nur `edumpi-dev` um
(Port 2222, Dev-Key, feste Host-Key-Prüfung) und ersetzen dabei einen mitgegebenen Port – die GUI ruft
fest `-p 22` auf. Andere Hosts gehen unverändert ans echte ssh; `scp` zwischen `edumpi-dev` und einem
anderen Host wird abgelehnt (in zwei Schritten kopieren).

## GUI gegen den Dev-Cluster

GUI aus einer Shell mit geladenem `dev/ssh/activate` starten (in Qt Creator/VS Code: `dev/ssh/bin` vorne
in den `PATH` der Run-Umgebung). Mac: aus dem Terminal starten, der Finder erbt den `PATH` nicht.

In Qt Creator: Projekte → Reiter **Einstellungen zur Ausführung** / **Run** (neben **Deployment-Einstellungen**) →
unten Block **Umgebung** → **Details** → Zeile `PATH` → **Pfad voranstellen…** / **Prepend Path…** mit
`/Pfad/zu/EduMPI_measurement/dev/ssh/bin`; ohne den Knopf **Bearbeiten** und den Pfad mit Doppelpunkt davorsetzen
(absolut, `cd dev/ssh/bin && pwd`).

| Formular | Feld | Wert |
| --- | --- | --- |
| Cluster | Hostname / Benutzer / Installationspfad | `edumpi-dev` / `dev` / `/opt/edumpi` |
| Datenbank | Host / Port | `127.0.0.1` / `5432` (bzw. `EDUMPI_DB_PORT`) |
| | DB / Benutzer / Passwort | `edumpi` / `edumpi` / `edumpi` |
| MPI Application | Number of Processes | höchstens „bis N“ aus der ersten Zeile von `compose.sh up` |

### Dev-Modus der GUI (Auto-Connect)

GUI mit `-DEDUMPI_DEV_MODE=ON` bauen (Release-Builds setzen das nie). Aktiv wird der Modus erst, wenn
`EDUMPI_DEV_ENV` auf `dev/gui.env` zeigt – das setzt `source dev/ssh/activate` (in Qt Creator zusätzlich
zum `PATH` als Variable in die Run-Umgebung).

| Verhalten | |
| --- | --- |
| Fenstertitel | „EduMPI [DEV]“; fehlt der SSH-Wrapper im `PATH`: „[DEV – SSH-Wrapper fehlt]“ |
| Start | verbindet automatisch erst die DB, dann den Cluster – Werte aus `gui.env` |
| Laufend | prüft alle 5 s und verbindet neu, z. B. nach `dev/compose.sh restart` oder wenn die Container erst nach der GUI starten |
| Formulare | mit den Dev-Werten vorausgefüllt (inkl. Passwort); manuell verbinden = ein Klick |
| Ohne Flag oder ohne `EDUMPI_DEV_ENV` | GUI wie bisher |

Angepasst an die GUI: Sie gibt ihren DB-Host an die Ranks weiter, deshalb zeigt `127.0.0.1:<DB-Port>` in
jedem Container per Weiterleitung auf die DB. Ihr `nix-shell -p --run "sbatch …"` bedient ein Ersatz ohne
Nix (`container/nix-shell`).

| GUI-Modus | Stand |
| --- | --- |
| EduMPI | geht, bis „bis N“ Ranks |
| ohne EduMPI | `mpicc`/`mpirun` aus dem `PATH` = reines Open MPI aus `/opt/ompi`, ohne Messung |
| OpenMP | bis `EDUMPI_NODE_CPUS` Threads (ein Node) |
| Score-P | nach `build-scorep.sh`; mit dem Open MPI des Forks noch nicht getestet |

## Ohne GUI testen

`examples/edumpi_test.c` erzeugt viel, wechselnde Kommunikation über alle Nodes (8 Ranks: alle 56 Paare,
bis ca. 6000 MPI-Aufrufe/s). Phasen reihum, Rank 0 schreibt jeden Wechsel mit Uhrzeit in die Ausgabe:

| Phase | Muster |
| --- | --- |
| Ring | Nachbar rechts |
| Verschiebung | Partner wechselt jede Runde |
| Hotspot | alle an Rank 0, der antwortet |
| Zufallspaare | jede Runde neue Paare |
| Kollektive | Bcast, Reduce, Allreduce, Allgather, Alltoall, Scatter, Gather |
| Ungleichgewicht | Wartezeiten, Late Sender |

Aufruf `./edumpi_test [Sekunden=60] [Sekunden je Phase=5]`; die GUI startet ohne Argumente.
`examples/job.sh` ist der Job wie aus der GUI (`EDUMPI_TEST_SECONDS`, `EDUMPI_TEST_PHASE`).

```sh
source dev/ssh/activate
ssh dev@edumpi-dev 'mkdir -p eduMPI_files'
scp dev/examples/edumpi_test.c dev/examples/job.sh dev@edumpi-dev:eduMPI_files/
ssh dev@edumpi-dev 'cd eduMPI_files && sbatch job.sh'
ssh dev@edumpi-dev 'cat eduMPI_files/slurm-*.out'
dev/compose.sh exec db psql -U edumpi -d edumpi -c 'select * from edumpi_runs'
```

## Wichtig zu wissen

| Thema | Hinweis |
| --- | --- |
| Messung | nur über `/opt/edumpi/bin/mpirun` (setzt `LD_PRELOAD`); Variablen in der README im Repo-Hauptordner |
| DB fehlt | DB nicht erreichbar oder ohne Schema → Ranks melden `edumpi: database connection failed` bzw. `COPY … failed` im Slurm-Log, DB prüfen |
| Run-ID | = Slurm-Job-ID; Slurm-Zustand liegt im Volume. DB und Slurm-Zustand nur gemeinsam zurücksetzen, sonst landen neue Läufe in alten Run-IDs |
| Zeiten | Cluster in der Zeitzone des Rechners, DB in UTC; das Tool schreibt echte Zeitpunkte (`to_timestamp`), `PGTZ` stört nicht |
| Sicherheit | `slurm/munge.key` und der SSH-Key sind feste Dev-Schlüssel; Ports nur auf `127.0.0.1`. Docker < 28 machte solche Ports im LAN erreichbar (`compose.sh` warnt) |
| Slurm | 23.11 aus Ubuntu 24.04, ohne cgroups (`slurm/cgroup.conf`), unprivilegiert |

## Zurücksetzen / Entfernen

| Was | Docker | Podman |
| --- | --- | --- |
| Stoppen (Daten bleiben) | `dev/compose.sh stop` | gleich |
| DB + Slurm-Zustand zurücksetzen | `dev/compose.sh down`, dann `docker volume rm edumpi-measurement_edumpi-db edumpi-measurement_slurm-state` | `podman volume rm …` |
| Alles inkl. Open MPI, Home, eigenem Image | `dev/compose.sh down -v --rmi local` | `dev/compose.sh down -v`, dann `podman rmi edumpi-measurement-dev:latest` |
| Fremd-Images (TimescaleDB, Basis-Image) | `docker rmi …` | `podman rmi …` |
