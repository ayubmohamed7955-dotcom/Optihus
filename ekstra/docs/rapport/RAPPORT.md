# Optihus — systemrapport (tekst)

**Version:** 0.3 (repo-synkroniseret)  
**Formål:** Beskrive hele det kørende Optihus-system — ikke kun hardwaredrivers — med sporbarhed til kildekode og databeskrivelse.

---

## 1. Resumé

Optihus består af to C++-noder:

1. **Pi 1 — `datahaandtering`** (central): TCP-server for feltnoden, web-UI, persistens i **SQLite**, konfigurations- og kommandostyring.
2. **Pi 2 — `miljoestyring`** (felt): TCP-klient, læser sensorer og styrer aktuatorer (vinduesmotor / pumpe), sender telemetri.

Begge programmer linker mod det delte bibliotek **`optihus_shared`** (`shared/`), som definerer **JSON-linje-protokollen** (`MessageEnvelope`, kodeks, typer, logger).

Kommunikation felt ↔ central sker som **newline-framed JSON** over TCP (**port 5000**, konstant **`kTcpPort`** i `shared/include/shared/constants.hpp`). Web-UI bruger HTTP på **port 8080** (**`kHttpUiPort`**).

---

## 2. Arkitekturdiagrammer

Alle diagrammer findes i **én PlantUML-fil**:

`docs/rapport/diagrams/optihus-system.puml`

Heri indgår blandt andet:

| Diagram-ID | Indhold |
|------------|---------|
| `optihus-index` | Oversigt + præcisionsnoter (GPIO, IIO, Hello/set_config) |
| `optihus-context` | C4-lignende systemkontekst |
| `optihus-repo-map` | Sammenhæng mellem CMake-targets og mapper |
| `optihus-bbd` | Blokdefinition (komponenthierarki) |
| `optihus-ibd-pi1` | Interne forbindelser på Pi 1 |
| `optihus-ibd-pi2` | Interne forbindelser på Pi 2 |
| `optihus-domain-contexts` | Domæner / bounded contexts |
| `optihus-er` | ER for SQLite (felter jf. `sql/init.sql`) |
| `optihus-sequence-handshake` | Hello → set_config → telemetri |
| `optihus-sequence-reconnect` | Genforbindelse + evt. `nmcli` recovery |
| `optihus-control-path` | Kritisk kontrolsti vs. sidekanaler |
| `optihus-state-motor` / `optihus-state-plantbox` | Konceptuelle tilstande |
| `optihus-deployment` | Systemd / netværk (eksempler) |
| `optihus-layers` | Lagmodel |

**Forbedr**inger i diagrampakken (if.t. tidligere udkast):

- **ER** er stemt overens med **`sql/init.sql`**: blandt andet `telemetry.source`, `active_config.timestamp`, `system_events.description`, `plant_boxes` tidsstempler, og fjernet vist FK mellem `command_history` og `active_config` (findes ikke i skemaet).
- **Pi 1-sekvens**: ved **Hello** kaldes `command_dispatcher_.dispatch_current_config()` (se `datahaandtering/src/app.cpp`); konfigurationen er forud indlæst ved opstart i `main.cpp`.
- **Pi 2-IBD**: `Motor`, **`PlantBox`**, **`DHT22`** (sysfs IIO), **`board_pins.hpp`**, persistens **`miljoestyring_field_config.json`**, telemetri **`field_live`** til Web-status **`field_node`**.
- **Pi 1 IBD / sekvens**: **`TelemetryProcessor`** leverer sidste felt **`field_live`** til **`WebUiServer`** (`GET /api/status` → **`field_node`**).

---

## 3. Pi 1 — datahaandtering

### 3.1 Opstart og afhængigheder

- **`datahaandtering/src/main.cpp`**: åbner database på **`kDbPath`** (`/opt/optihus/data/optihus.db` i `shared/include/shared/constants.hpp`), kører `initialize_schema("sql/init.sql")` (sti relativt **arbejdskatalog**), indlæser aktiv config via `ConfigManager`, starter køer, `TcpServer`, `WebUiServer`, `TelemetryProcessor`, `App`.
- **`App::run()`** (`datahaandtering/src/app.cpp`): starter TCP og web-servere, `TelemetryProcessor`, derefter blokerende løkke på **`inbound_queue`**.

### 3.2 Beskedflow

- **`TcpServer`** læser linjer fra klienter, dekoder til `MessageEnvelope`, lægger i **`inbound_queue`**.
- **`App::handle_message`** (`app.cpp`):
  - **Hello:** `command_dispatcher_.dispatch_current_config()` — sender **set_config** til felt.
  - **Telemetry:** `telemetry_queue_.push` — asynkron behandling via **`TelemetryProcessor`** → **`DatabaseManager.insert_telemetry`**; hvis payload indeholder **`field_live`**, opdateres **`WebUiServer::note_field_live`** (vises under **`GET /api/status`** som **`field_node`**).
- **`CommandDispatcher`** + **`outbound_queue`** sender kommandoer retur til den korrekte klient-session (detaljer: `tcp_server.cpp` / dispatcher).

### 3.3 Web-UI og TUI

- Web-UI: **`WebUiServer`** på port 8080; **`GET /api/status`** inkluderer driftfelter og **`field_node`** (seneste Pi 2 komponent-/TCP‑snapshot fra telemetri **`field_live`**), primært til fejlsøgning i browseren.
- **`--tui`**: alternativ grænseflade (`TuiManager`), samme database — vist i `optihus-control-path` som “sidekanal” ift. TCP-kontrolstien.

---

## 4. Pi 2 — miljøstyring

### 4.1 Opstart og persistens

- **`miljoestyring/src/main.cpp`**: kalder `App::run()`.
- **`App::run()`** (`miljoestyring/src/app.cpp`), groft rækkefølge:
  1. Indlæs **`miljoestyring_config.json`** (server‑host/port); standard **`192.168.4.1:5000`** hvis fil mangler (log).
  2. Ved **`OPTIHUS_MILJOESTYRING_HARDWARE`**: opret **`Motor`** og **`PlantBox`** med pins fra **`include/hw/board_pins.hpp`** (partner‑zip layout: stepper **17–22‑familien**, pumpe **16**, **`DHT22_GPIO_PIN`** til IIO‑sti); **`motor.start()`** / **`plant_box.start()`** så sensor‑loops kører fra boot.
  3. **`SensorManager.set_hardware`**; **`ClientRuntimeState`** + **`ControlManager`**.
  4. **`load_field_config_for_replay()`** (`set_config_store.cpp`): hvis **`miljoestyring_field_config.json`** findes (version 2), syntetiseres **`set_config`** og anvendes **før** TCP‑forbindelse (genopret grænser, **`server_config_received`**, aktuation vs. nødstop‑overlay).
  5. **`TcpClient`** + reconnect‑lambda; hovedløkke: drain **`ControlManager`**, **`send_telemetry`** (kan indeholde **`field_live_json`**), vent **`telemetry_interval_sec`** (mindst **1 s** pr. sekund i indre venteløkke).

### 4.2 Hardware og kontrol (kort)

- **`Motor`** (`include/hw/Ventilation system/`, `libgpiod`): baggrundstråd læser luft temp/fugt via **`DHT22::update()`** (sysfs **IIO** under `/sys/bus/iio/.../iio:device0/`). **`setActuationEnabled`**: false indtil **`set_config`** uden aktiv nødstop — så kan stepper åbne/lukke vindue efter grænser fra **`temp_bounds_c`** / **`bounds`**.
- **`PlantBox`** (`include/hw/Watering system/`): I2C **`/dev/i2c-1`**, **`0x48`**; pumpe via **`pinctrl`**; **`setActuationEnabled`** styrer om vanding må køre (fail‑safe / pre‑konfiguration).
- **`ControlManager`**: **`set_config`** (interval, grænser, plant_boxes, **`persist_field_config_after_set_config`**); **`emergency_stop`** / **`clear_emergency_stop`** med **`persist_field_config_emergency_overlay`** når cachefil findes.
- **`SensorManager`**: bygger **`TelemetrySample`** (bl.a. **`pump_status`**, **`motor_status`**, valgfri **`field_live_json`** til web‑**`field_node`**).

### 4.3 DHT og IIO

**`DHT22`** læser ikke GPIO bit‑banged; den forventer kernedriver/overlay der eksponerer **IIO**‑sysfs (juster **`DHT22_GPIO_PIN`** i **`board_pins.hpp`** og evt. **`/boot/firmware/config.txt`** så de matcher).

---

## 5. Data og protokol

### 5.1 SQLite

- Init-skema: [sql/init.sql](../../sql/init.sql).
- Vigtige tabeller: `telemetry`, `profiles`, `active_config`, `plant_boxes`, `system_events`, `command_history`.
- `DatabaseManager` kan migrere/rette skema (fx `telemetry.temperature_c`, `plant_boxes`-kolonner) — se `datahaandtering/src/database_manager.cpp`.

### 5.2 Beskedtyper

Defineret i `shared/include/shared/message.hpp` og serialiseret i `shared/src/message_codec.cpp`. Indkommende linjer skal have gyldig **`ts`** i UTC ISO-8601 (`YYYY-MM-DDTHH:MM:SSZ`) for at blive accepteret i klientens dekoder (`decode_json_line`).

**Telemetri:** Ud over klassiske felter kan Pi 2 sende valgfrit JSON‑objekt **`field_live`** (komponent-/TCP‑status); det persisteres **ikke** i SQLite i v1, men spejles i **`GET /api/status`** (`field_node`) via **`TelemetryProcessor`** → **`WebUiServer`**.

---

## 6. Deployment og drift

- **Database-sti:** `kDbPath` i `shared/include/shared/constants.hpp`.
- **Pi 2 standardnetværk:** feltnoden forventer Pi 1 på **`192.168.4.1:5000`** (AP‑tilstand). Ved anden topologi: ret **`kDefaultDataServerHost`** / **`kTcpPort`** i `shared/include/shared/constants.hpp` og genbyg.
- **Systemd:** skabeloner under [vejledning/systemd](../../vejledning/systemd) (`*.example`, `skabelon-*`).
- **CMake:** rod `CMakeLists.txt` tilføjer `shared`, og valgfrit `datahaandtering` / `miljoestyring`. Miljøstyring kan bygges uden hardware (`OPTIHUS_MILJOESTYRING_HARDWARE=OFF` på ikke-Linux eller ved cmake-option).

---

## 7. Konklusion for rapportlæseren

Systemet er **ikke** “hardwarefiler alene”: feltnoden kræver **TCP-klient**, **kommando-fortolkning**, **telemetri**, og **delte protokolfiler** for at fungere som en del af Optihus. Pi 1 kræver tilsvarende server-, persistens- og UI-lag.

---

## 8. Kildehenvisninger (primære)

| Emne | Sti |
|------|-----|
| Pi 1 main / wiring | `datahaandtering/src/main.cpp`, `datahaandtering/src/app.cpp` |
| Pi 2 main / wiring | `miljoestyring/src/main.cpp`, `miljoestyring/src/app.cpp` |
| Kommando & config (felt) | `miljoestyring/src/control_manager.cpp` |
| TCP (felt) | `miljoestyring/src/tcp_client.cpp` |
| Database-skema | `sql/init.sql` |
| Delt protokol | `shared/include/shared/*.hpp`, `shared/src/message_codec.cpp` |
| Konstanter (porte, stier) | `shared/include/shared/constants.hpp` |
| Felt persistens | `miljoestyring/include/set_config_store.hpp`, `miljoestyring/src/set_config_store.cpp` |
| GPIO-layout (Pi 2) | `miljoestyring/include/hw/board_pins.hpp` |

Diagrammer: `docs/rapport/diagrams/optihus-system.puml`.
