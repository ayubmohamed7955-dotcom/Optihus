# Optihus - Dokumentationsudkast

Dette dokument er et samlet udkast til rapport og driftshåndbog for Optihus.

## 1) Projektbeskrivelse

Optihus er et lokalt, distribueret drivhusstyringssystem bygget på to Raspberry Pi-enheder:

- **Datahåndtering (AP/server-node)**: hotspot, TCP-server, SQLite, TUI, visualisering.
- **Miljøstyring (klient-node)**: forbinder til hotspot, sender telemetri, modtager kommandoer, styrer sensorer/aktuatorer.

Formålet er robust lokal drift uden cloud-afhængighed.

## 2) Softwarearkitektur

| Lag | Ansvar | Centrale filer |
|---|---|---|
| Shared | Fælles kontrakter, JSON line codec, tid, logging | `shared/include/shared/*`, `shared/src/message_codec.cpp` |
| Datahåndtering | TCP-server, DB, command dispatch, TUI | `datahaandtering/src/main.cpp`, `app.cpp`, `tcp_server.cpp`, `database_manager.cpp`, `command_dispatcher.cpp` |
| Miljøstyring | TCP-klient, sensorlæsning (stub), kontrol-loop | `miljoestyring/src/main.cpp`, `app.cpp`, `tcp_client.cpp`, `sensor_manager.cpp`, `control_manager.cpp` |

## 3) Netværksarkitektur

### Topologi

- AP/server Pi: hoster Wi-Fi AP og TCP-server.
- Klient Pi: auto-connect til AP og opretter TCP-klientforbindelse.

### Aftalte netværksparametre

| Felt | Værdi |
|---|---|
| SSID | `Optihus` |
| WPA2-password | `OptihuS123` |
| AP IP | `192.168.4.1/24` |
| TCP-port | `5000` |

### Designprincip

- Systemet skal fungere lokalt uden internet.
- TCP bruges som én persistent kanal med newline-delimiterede JSON-beskeder.

## 4) Installationsguide - Windows (miljoestyring)

Forudsætning: MSYS2 UCRT64 + CMake + Ninja + `nlohmann_json`.

```powershell
cmake -S . -B build-windows -G Ninja `
  -DOPTIHUS_BUILD_DATAHAANDTERING=OFF `
  -DOPTIHUS_BUILD_MILJOESTYRING=ON `
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe

cmake --build build-windows --target miljoestyring
```

Kørsel:

```powershell
.\build-windows\miljoestyring\miljoestyring.exe
```

## 5) Installationsguide - Raspberry Pi

### AP/server Pi (Datahåndtering)

```bash
sudo nmcli con add type wifi ifname wlan0 con-name Optihus-AP autoconnect yes ssid Optihus
sudo nmcli con modify Optihus-AP 802-11-wireless.mode ap 802-11-wireless.band bg
sudo nmcli con modify Optihus-AP ipv4.addresses 192.168.4.1/24
sudo nmcli con modify Optihus-AP ipv4.method shared
sudo nmcli con modify Optihus-AP ipv6.method ignore
sudo nmcli con modify Optihus-AP wifi-sec.key-mgmt wpa-psk
sudo nmcli con modify Optihus-AP wifi-sec.psk "OptihuS123"
sudo nmcli con up Optihus-AP
```

### Klient Pi (Miljøstyring)

```bash
sudo nmcli dev wifi connect "Optihus" password "OptihuS123" ifname wlan0
sudo nmcli con modify "Optihus" connection.autoconnect yes connection.autoconnect-priority 100
```

Verifikation:

```bash
sudo nmcli dev status
ip addr show wlan0
ip route
ping -c 4 192.168.4.1
```

## 6) Byggeguide

### Raspberry Pi

```bash
cmake -S . -B build
cmake --build build -j4
```

### Kun Datahåndtering

```bash
cmake -S . -B build -DOPTIHUS_BUILD_MILJOESTYRING=OFF
cmake --build build --target datahaandtering -j4
```

### Kun Miljøstyring

```bash
cmake -S . -B build -DOPTIHUS_BUILD_DATAHAANDTERING=OFF
cmake --build build --target miljoestyring -j4
```

## 7) Testguide

### Basal TCP startup-test

1. Start server på AP Pi:
   - `./build/datahaandtering/datahaandtering`
2. Start klient:
   - `./build/miljoestyring/miljoestyring` (Pi) eller `miljoestyring.exe` (Windows)
3. Bekræft server-log:
   - klient connected
   - ingen invalid JSON warning

### Funktionelle testpunkter

| Test | Forventning |
|---|---|
| Hello handshake | Server modtager `hello`, sender config-kommando |
| Telemetri-format | JSON line med gyldig `type`, `ts`, `source` |
| DB insert | Telemetri ender i `telemetry`-tabel |
| TUI config ændring | `active_config` opdateres i DB |

## 8) Fejlsøgningsguide

| Problem | Typisk årsag | Handling |
|---|---|---|
| `Dropped invalid JSON line` | Forkert `ts` format eller manglende felt | Brug `YYYY-MM-DDTHH:MM:SSZ`, medtag `type`/`source` |
| Ingen TCP-forbindelse | Forkert IP/port eller AP ikke aktiv | Verificér `ip a`, `nmcli`, ping AP IP |
| Server abort ved connect | Thread-lifecycle-fejl | Brug opdateret `tcp_server.cpp` med sikker join/stop |
| Windows build fejler med generator | VS/MSVC mangler | Brug Ninja + MSYS2 UCRT64 eller installer VS Build Tools |

## 9) Team-kontrakt (sensorer, aktuatorer, beskeder)

### Sensorer og enheder

| Felt | Betydning | Enhed |
|---|---|---|
| `humidity_air` | Luftfugtighed | `%RH` |
| `humidity_soil` | Jordfugtighed | `%` (kalibreret) |
| `temperature_air` | Lufttemperatur | `degC` |

### Aktuatorer

| Felt | Gyldige værdier |
|---|---|
| `pump_status` | `off`, `on` |
| `motor_status` | `closed`, `open`, `opening`, `closing`, `stopped` |

### Beskeder (obligatoriske envelope-felter)

| Felt | Krav |
|---|---|
| `type` | lowercase enum (`hello`, `telemetry`, `command`, ...) |
| `ts` | ISO-8601 UTC (`YYYY-MM-DDTHH:MM:SSZ`) |
| `source` | `datahaandtering` eller `miljoestyring` |

## 10) Fremtidige forbedringer

1. Implementér rigtig sensor-driver (erstat nuværende stub-værdier).
2. Tilføj temperatur til shared kontrakt + DB schema + Grafana dashboards.
3. Implementér fuld klient-side protokol:
   - telemetri-sending
   - command receiver
   - heartbeat.
4. Flyt netværkskonfig til robust runtime-config på begge Pi’er.
5. Tilføj integrationstests for JSON kontrakt og socket-flow.

