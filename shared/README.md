# Shared mappe

Denne mappe indeholder fælles kode, som bruges af både server og klient.

## Hvad den gør

- Definerer fælles typer og beskedformat
- Koder/dekoder JSON-lines beskeder
- Hjælpefunktioner til logging og tid

## Vigtige undermapper

- `include/shared/` = fælles headers
- `src/` = fælles implementation

## Vigtige filer

- `include/shared/message.hpp` - beskedmodeller og payloads (Hello, Telemetry med valgfrit **`field_live`**, Command, Ack)
- `include/shared/constants.hpp` - fælles konstanter (port **5000** og **8080**, database-sti, genforbindelsesforsinkelse)
- `include/shared/types.hpp` - datafelter for profiler, plantekasser og telemetri-prøver (inkl. valgfri **`field_live_json`** på Pi 2)
- `include/shared/thread_safe_queue.hpp` - sikker kø mellem tråde på server og klient
- `src/message_codec.cpp` - JSON-linje encode/decode til TCP-ledningen
- `src/logger.cpp` - log output
- `src/time_utils.cpp` - tidsformat i UTC

Se også **`vejledning/optihus-fuld-forklaring-dansk.md`** afsnit om `shared/` for en rolig gennemgang uden kode.
