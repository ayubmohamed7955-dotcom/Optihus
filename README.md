# Optihus (Starter Skeleton)

Optihus is a two-node Raspberry Pi greenhouse project:

- `datahaandtering-rpi` (central node): TCP server, SQLite, TUI, Grafana integration.
- `miljoestyring-rpi` (field node): TCP client, sensors, actuators, control loop.

This repository currently contains the **Phase 2 starter skeleton**.

## Requirements (Raspberry Pi OS Lite 64-bit)

Install on both Pis:

- `build-essential`
- `cmake`
- `sqlite3`
- `libsqlite3-dev`
- `nlohmann-json3-dev`

Additionally on Datahåndtering:

- `libncurses-dev`
- Grafana + `frser-sqlite-datasource`

## Build

```bash
cmake -S . -B build
cmake --build build -j4
```

## Runtime model (realistic TUI over SSH)

The simplest practical setup for Raspberry Pi OS Lite is:

1. Run `datahaandtering` backend as a normal process or systemd service (non-interactive).
2. User SSHes into `datahaandtering-rpi`.
3. User starts TUI manually:

```bash
./build/datahaandtering/datahaandtering --tui
```

This avoids fragile auto-launch behavior in non-interactive service contexts and works reliably over SSH.

## Connection and reconnect rule

- Miljøstyring keeps one persistent TCP connection to Datahåndtering (`port 5000`).
- On reconnect:
  - client re-sends `hello`
  - server re-sends current `active_config`

## Timestamps

- Protocol messages use `ts` in ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`).
- SQLite timestamps are stored as ISO-8601 UTC text in all tables.

## SQL setup

Initialize DB schema:

```bash
sqlite3 /opt/optihus/data/optihus.db < sql/init.sql
sqlite3 /opt/optihus/data/optihus.db < sql/seed_profiles.sql
```

Schema includes:

- `telemetry`
- `system_events`
- `profiles`
- `active_config`
- `command_history`
