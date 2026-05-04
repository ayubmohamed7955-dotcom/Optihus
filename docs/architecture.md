# Optihus Architecture (Phase 1 Refined)

## Core setup

- One persistent TCP connection between:
  - `datahaandtering-rpi` (server)
  - `miljoestyring-rpi` (client)
- Shared JSON-lines protocol (`1 JSON object per line`).
- SQLite on Datahåndtering at `/opt/optihus/data/optihus.db`.
- Grafana reads SQLite directly via `frser-sqlite-datasource`.

## Reconnect behavior

When the client reconnects:

1. Client sends `hello` again.
2. Server responds by re-sending the current active config.

## Timestamp standard

- All protocol timestamps are ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`).
- All database timestamps are stored as ISO-8601 UTC text.

## TUI startup model over SSH

Practical model for Raspberry Pi OS Lite:

- Backend runs headless (`datahaandtering` process/service).
- User opens SSH terminal to Datahåndtering.
- User launches interactive TUI explicitly:
  - `./datahaandtering --tui`

This model is robust and easy to explain in a report/demo.