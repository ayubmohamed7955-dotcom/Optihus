# Miljøstyring (RPi2-klient)

Klientdelen af Optihus: TCP-mod `datahaandtering`, JSON-linjer som i `shared::MessageEnvelope`.

**Mappe til headers:** `miljoestyring/include/` (fx `app.hpp`, `tcp_client.hpp`) og `miljoestyring/include/hw/` til hardware — ikke længere `include/miljoestyring/...` (dobbelt navn).

## Hvad den gør

- Forbinder til server (standard `192.168.4.1:5000` eller `miljoestyring_config.json`)
- Sender `Hello` efter connect
- **Læser** indkommende `Command` (særligt `set_config`, `emergency_stop`, `clear_emergency_stop`) i baggrundstråd
- Sender `Telemetry` med jævne mellemrum (`interval_sec` fra server, default 900 s hvis ikke sat)
- Reconnect ved fejl

## Hardware (Linux / Raspberry Pi)

CMake-option **`OPTIHUS_MILJOESTYRING_HARDWARE`** (default **ON** på Linux, **OFF** på Windows):

- **`hw::DHT22`** (`DHT22.hpp` / `DHT22.cpp`) — lufttemperatur og luftfugtighed (IIO), samme API-navne som i zip (`getTemperature`, `getHumidity`, `update`).
- **`hw::Motor`** — stepper til vindue; metodenavne som i zip (`setTemperatureLimits`, `getCurrentTemp`, `open`/`close`, `SEQ`, `STEPS_PER_REV`).
- **`hw::PlantBox`** — jordfugt via **I2C** @ `0x48`, pumpe via `pinctrl`; som zip (`setMinimumThreshold`, `getCurrentAverageMoisture`, …).

På maskiner uden `libgpiod`: konfigurer med `-DOPTIHUS_MILJOESTYRING_HARDWARE=OFF` (stub-telemetry).

## `set_config` fra RPi1

Ud over `profile_name`, `profile_names`, `interval_sec`, `emergency_stop` kan serveren sende:

- **`bounds`**: `min_humidity_air`, `max_humidity_air`, `min_humidity_soil`, `max_humidity_soil` (aggregeret fra profiler i databasen)
- **`temp_bounds_c`**: `min` / `max` for vinduesmotor (foreløbig standard 12–35 °C, indtil profiler får temperaturfelter)

## Flere plantekasser (fremtid)

Anbefaling: **én JSON-linje pr. `Telemetry`-besked** (som nu). Ved flere kasser: enten **flere linjer i samme tidsinterval** (evt. med ekstra felt `box_id` i payload, når RPi1/DB understøtter det), eller ét udvidet payload — vælg én model og hold den synkron med `telemetry_from_json` / database.

## Konfigurationsfil

`miljoestyring_config.json` i **nuværende arbejdsmappe** ved start:

```json
{
  "server_host": "192.168.4.1",
  "server_port": 5000
}
```

## Byg

Fra repo-rod: `cmake -B build && cmake --build build --target miljoestyring`
