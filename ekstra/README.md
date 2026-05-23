# Optihus

Optihus is a two-node greenhouse control project built around a persistent TCP link:

- `datahaandtering-rpi` (Pi 1, central node): AP, TCP server (`5000`), SQLite, logging, backend command handling, Web ui (`8080`).
- `miljoestyring-rpi` (Pi 2, control node): TCP client/controller, sensor/actuator control loop, reconnect/retry until Pi 1 is reachable.

This repository is intended to be cloned on both devices and built locally.

## Repository structure (visual)

```text
Optihus/
|- README.md
|- CMakeLists.txt
|- .gitignore
|- miljoestyring_config.json.example
|- datahaandtering/
|  |- README.md
|  |- CMakeLists.txt
|  |- include/
|  `- src/
|- miljoestyring/
|  |- README.md
|  |- CMakeLists.txt
|  |- include/
|  |  |- set_config_store.hpp
|  |  `- hw/
|  |     |- board_pins.hpp
|  |     |- ClimateControl/
|  |     `- WateringSystem/
|  `- src/
|- shared/
|  |- README.md
|  |- CMakeLists.txt
|  |- include/
|  `- src/
|- sql/
|  |- README.md
|  |- init.sql
|  `- seed_profiles.sql
|- docs/
|  `- rapport/
|     |- README.md
|     |- RAPPORT.md
|     `- diagrams/
|        `- optihus-system.puml
|- vejledning/
|  |- README.md
|  |- dokumentation-indeks.md
|  |- boot-og-netvaerk-da.md
|  |- skabeloner-pi1-pi2-da.md
|  |- md-struktur.md
|  |- optihus-fuld-forklaring-dansk.md
|  |- quickstart-for-team-da.md
|  |- setup-pi1-datahaandtering-da.md
|  |- setup-pi2-miljoestyring-da.md
|  |- integration-test-checklist-da.md
|  |- architecture.md
|  |- optihus-dokumentation-udkast.md
|  |- tui-til-browser-ui-forhindringer-og-loesninger.md
|  |- windows/
|  |  `- build-windows-da.md
|  `- systemd/
|     |- optihus-datahaandtering.service.example
|     |- optihus-miljoestyring.service.example
|     |- skabelon-rpi1-datahaandtering.service
|     `- skabelon-rpi2-miljoestyring.service
```

Notes:

- Build output folders (like `build/`, `build-windows/`, `build-windows-tcp/`) are intentionally **not** part of the repository.
- Use `miljoestyring_config.json.example` as the template when creating a local runtime config.

## Read this first

If you are new to the project, use this order:

1. `vejledning/optihus-fuld-forklaring-dansk.md` (complete Danish explanation; **section 3** is the structural overview with diagrams)
2. `docs/rapport/RAPPORT.md` + `docs/rapport/diagrams/optihus-system.puml` (report text and PlantUML architecture diagrams)
3. `vejledning/quickstart-for-team-da.md` (simple start/stop guide)
4. **`vejledning/setup-pi1-datahaandtering-da.md`** and **`vejledning/setup-pi2-miljoestyring-da.md`** (step-by-step Pi setup; assumes repo already on device)
5. `vejledning/integration-test-checklist-da.md` (how to verify TCP works)
6. `vejledning/architecture.md` (technical overview)
7. `vejledning/optihus-dokumentation-udkast.md` (report draft, Danish)

## Current validated network mode

The main demo mode is fixed AP mode:

- Server AP IP: `192.168.4.1`
- TCP port: `5000`
- Web ui port: `8080`
- Protocol: JSON-lines (one JSON object per line)

Cross-device TCP has been tested in two network scenarios:

- Routed LAN (`172.16.15.x`)
- AP LAN (`192.168.4.x`)

## Requirements

### Raspberry Pi (both nodes)

- `build-essential`
- `cmake`
- `sqlite3`
- `libsqlite3-dev`
- `nlohmann-json3-dev`

### Datahåndtering node only

- `libncurses-dev`
- Grafana + SQLite datasource plugin (exact package depends on your Grafana version; optional dashboards)

## Build

```bash
cmake -S . -B build
cmake --build build -j4
```

## Run (AP mode, cross-device)

### 1) Server (Datahåndtering RPi)

```bash
./build/datahaandtering/datahaandtering
```

Expected startup log includes:

- `Listening on port 5000`
- `Web ui listening on port 8080`

### 2) Client (Miljøstyring node)

Set `miljoestyring_config.json` so `server_host` points to server AP IP `192.168.4.1`, then run:

```bash
./build/miljoestyring/miljoestyring
```

or on Windows:

```powershell
.\build-windows-tcp\miljoestyring\miljoestyring.exe
```

Expected client log includes:

- `Connected and sent hello to 192.168.4.1:5000`

Expected server log includes:

- `Client connected: 192.168.4.x`
- `Received hello. Sending active config.`

### 3) Human interface (Laptop on AP)

Connect laptop to Pi 1 AP and open:

- `http://192.168.4.1:8080`

Fallback procedure if interface does not open automatically:

1. Open browser manually and go to `http://192.168.4.1:8080`.
2. If Web ui is not reachable, run local fallback/admin TUI on Pi 1:
   - `./build/datahaandtering/datahaandtering --tui`

Interface policy:

- Web ui is primary for status/control.
- TUI is fallback/admin only.
- Grafana is optional read-only overview (never command/control path).

## Boot reliability rule

The system should be user-friendly and startup-order tolerant:

- User can power nodes in any order.
- Client must auto-recover/reconnect when server/AP becomes available.
- No manual process restart should be required during normal startup.

This is checked in the integration test checklist.

## Control-path rule (no architecture drift)

- Keep Pi 1 as central brain.
- Keep control flow through existing backend + TCP path.
- Keep UI practical/minimal.
- Do not add cloud services or extra control layers in critical path.

## Timestamps and data format

- Protocol timestamps: ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`)
- SQLite timestamps: ISO-8601 UTC text

## SQL setup

Initialize DB schema:

```bash
sqlite3 /opt/optihus/data/optihus.db < sql/init.sql
sqlite3 /opt/optihus/data/optihus.db < sql/seed_profiles.sql
```

Core tables:

- `telemetry`
- `system_events`
- `profiles`
- `active_config`
- `command_history`

## Documentation map

- Rapport + PlantUML‑diagrammer: **`docs/rapport/`** ([RAPPORT.md](docs/rapport/RAPPORT.md), [diagrams/optihus-system.puml](docs/rapport/diagrams/optihus-system.puml))
- Main docs index: `vejledning/dokumentation-indeks.md`
- Full Danish system explanation: `vejledning/optihus-fuld-forklaring-dansk.md`
- Technical architecture: `vejledning/architecture.md`
- Danish operations/report draft: `vejledning/optihus-dokumentation-udkast.md`
- Non-technical quick guide (Danish): `vejledning/quickstart-for-team-da.md`
- Integration checklist (Danish): `vejledning/integration-test-checklist-da.md`
- Raspberry Pi boot / systemd / network (Danish): `vejledning/boot-og-netvaerk-da.md`
- Windows CMake build (Danish): `vejledning/windows/build-windows-da.md`
