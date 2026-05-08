# Optihus Architecture

## System roles

- `datahaandtering-rpi`: central node. Runs TCP server on port `5000`, stores data in SQLite, can run TUI.
- `miljoestyring-rpi`: field node. Runs TCP client and control loop.

## Network model

- Preferred setup: fixed AP mode with server/AP IP `192.168.4.1`.
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

## Data and timestamp conventions

- Protocol `ts`: ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`)
- SQLite timestamps: ISO-8601 UTC text
- DB path on server: `/opt/optihus/data/optihus.db`

## Startup reliability principle

Goal is startup that works in any order:

- User should be able to power nodes in any order.
- System should recover automatically when network/server is ready.
- No manual restart should be required in normal operation.

## Demo/integration proof indicators

A valid cross-device run must show all of:

- Server listens on `:5000`
- Client targets real server IP (not loopback)
- Server logs real client IP
- Socket state shows `ESTABLISHED` between two different hosts