# Miljoestyring mappe

Denne mappe er klient-delen af Optihus.

## Hvad den gør

- Forbinder til serverens TCP port `5000`
- Sender handshake (`hello`)
- Kører sensor/kontrol-loop

## Vigtige undermapper

- `src/` = implementation (`.cpp` filer)
- `include/` = headers (`.hpp` filer)

## Vigtige filer

- `src/main.cpp` - opstart af klient-app
- `src/tcp_client.cpp` - connect/send/reconnect logik
- `src/app.cpp` - runtime flow og netværkskonfig
- `CMakeLists.txt` - build-definition for target `miljoestyring`

## Konfigurationsfil

Klienten læser `miljoestyring_config.json` fra repo-roden.

Eksempel (AP mode):

```json
{
  "server_host": "192.168.4.1",
  "server_port": 5000
}
```
