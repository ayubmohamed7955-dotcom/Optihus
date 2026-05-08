# Datahaandtering mappe

Denne mappe er server-delen af Optihus.

## Hvad den gør

- Starter TCP-server på port `5000`
- Gemmer data i SQLite
- Behandler beskeder fra klienten
- Kan køre TUI lokalt på serveren

## Vigtige undermapper

- `src/` = implementation (`.cpp` filer)
- `include/` = headers (`.hpp` filer)

## Vigtige filer

- `src/main.cpp` - opstart og wiring af komponenter
- `src/tcp_server.cpp` - TCP server, accept/read/write loops
- `src/database_manager.cpp` - database adgang
- `src/app.cpp` - overordnet runtime flow
- `CMakeLists.txt` - build-definition for target `datahaandtering`

## Typisk kørsel (Pi)

```bash
./build/datahaandtering/datahaandtering
```

Forventet log:

- `Listening on port 5000`
