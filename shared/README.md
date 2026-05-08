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

- `include/shared/message.hpp` - beskedmodeller
- `include/shared/constants.hpp` - fælles konstanter (port, host-navne, m.m.)
- `src/message_codec.cpp` - JSON encode/decode
- `src/logger.cpp` - log output
- `src/time_utils.cpp` - tidsformat i UTC
