# Optihus

Optihus is a two-node greenhouse control project built around a persistent TCP link:

- `datahaandtering-rpi` (server/AP node): TCP server, SQLite, backend, optional TUI/Grafana.
- `miljoestyring-rpi` (client node): TCP client, sensor/control loop.

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
`- docs/
   |- README.md
   |- md-struktur.md
   |- quickstart-for-team-da.md
   |- integration-test-checklist-da.md
   |- architecture.md
   `- optihus-dokumentation-udkast.md
```

Notes:

- Build output folders (like `build/`, `build-windows/`, `build-windows-tcp/`) are intentionally **not** part of the repository.
- Use `miljoestyring_config.json.example` as the template when creating a local runtime config.

## Read this first

If you are new to the project, use this order:

1. `docs/quickstart-for-team-da.md` (simple start/stop guide)
2. `docs/integration-test-checklist-da.md` (how to verify TCP works)
3. `docs/architecture.md` (technical overview)
4. `docs/optihus-dokumentation-udkast.md` (report draft, Danish)

## Current validated network mode

The main demo mode is fixed AP mode:

- Server AP IP: `192.168.4.1`
- TCP port: `5000`
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
- Grafana + `frser-sqlite-datasource` (optional for dashboards)

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

## Boot reliability rule

The system should be user-friendly and startup-order tolerant:

- User can power nodes in any order.
- Client must auto-recover/reconnect when server/AP becomes available.
- No manual process restart should be required during normal startup.

This is checked in the integration test checklist.

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

- Main docs index: `docs/README.md`
- Technical architecture: `docs/architecture.md`
- Danish operations/report draft: `docs/optihus-dokumentation-udkast.md`
- Non-technical quick guide (Danish): `docs/quickstart-for-team-da.md`
- Integration checklist (Danish): `docs/integration-test-checklist-da.md`
