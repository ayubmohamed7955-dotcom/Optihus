# Optihus Architecture

## System roles

- `datahaandtering-rpi` (Pi 1): central node. Hosts AP, runs TCP server on `5000`, stores data in SQLite, logs events, handles backend commands, and serves browser UI on `8080`.
- `miljoestyring-rpi` (Pi 2): control node. Runs TCP client/controller with sensor/actuator logic and keeps retrying until Pi 1 is reachable.

Human interfaces:

- Browser UI on Pi 1 is primary.
- TUI is fallback/admin only.
- Grafana is optional read-only (graphs/trends only, never control path).

## Network model

- Preferred setup: fixed AP mode with server/AP IP `192.168.4.1`.
- Laptop joins Pi 1 AP and opens browser UI on `http://192.168.4.1:8080`.
- One persistent TCP channel between nodes.
- Message format: JSON-lines (`1 JSON envelope per newline`).

## Handshake and reconnect behavior

Connection contract:

1. Client connects to server.
2. Client sends `hello`.
3. Server answers by sending active configuration.

Reconnect contract:

- If client reconnects, it sends `hello` again.
- Server re-sends current active config.
- System must tolerate either startup order (Pi 1 first or Pi 2 first).
- System must not depend on captive-portal/browser auto-launch behavior.

## Data and timestamp conventions

- Protocol `ts`: ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`)
- SQLite timestamps: ISO-8601 UTC text
- DB path on server: `/opt/optihus/data/optihus.db`

## Startup reliability principle

Goal is startup that works in any order:

- User should be able to power nodes in any order.
- System should recover automatically when network/server is ready.
- No manual restart should be required in normal operation.

## Control-path guardrails

- Keep Pi 1 as the central "brains" node.
- Keep command/control through existing backend + TCP path only.
- Keep browser UI minimal/practical.
- Avoid architecture drift (no cloud or extra control layer in critical path).

## Demo/integration proof indicators

A valid cross-device run must show all of:

- Server listens on `:5000`
- Client targets real server IP (not loopback)
- Server logs real client IP
- Socket state shows `ESTABLISHED` between two different hosts