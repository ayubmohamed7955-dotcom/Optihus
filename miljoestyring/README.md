# Miljøstyring (RPi2-klient)

Klientdelen af Optihus: TCP-mod `datahaandtering`, JSON-linjer som i `shared::MessageEnvelope`.

**Mappe til headers:** `miljoestyring/include/` (f.eks. `app.hpp`, `tcp_client.hpp`) og `miljoestyring/include/hw/` til hardware; ikke længere `include/miljoestyring/...` (dobbelt navn).

## Hvad den gør

- Forbinder til server (standard `192.168.4.1:5000` eller `miljoestyring_config.json`)
- Sender `Hello` efter connect
- **Læser** indkommende `Command` (særligt `set_config`, `emergency_stop`, `clear_emergency_stop`) i baggrundstråd
- Sender `Telemetry` med jævne mellemrum (`interval_sec` fra server, default **900 s** hvis ikke sat)
- Reconnect ved fejl uden at vente på næste telemetri-interval
- På Linux: periodisk Wi-Fi recovery under længere disconnect (`nmcli radio wifi on` + `nmcli dev wifi rescan`)

## Hardware (Linux / Raspberry Pi)

CMake-option **`OPTIHUS_MILJOESTYRING_HARDWARE`** (default **ON** på Linux, **OFF** på Windows):

Der findes **ingen** separat samlet “aktuator manager”‑klasse i buildet; pumpe og vindue håndteres direkte af **`PlantBox`** og **`Motor`** i `control_manager`‑flowet.

- **`hw::DHT22`** (`DHT22.hpp` / `DHT22.cpp`): lufttemperatur og luftfugtighed (IIO), API: `getTemperature`, `getHumidity`, `update`.
- **`hw::Motor`**: stepper til vindue, API: `setTemperatureLimits`, `getCurrentTemp`, `open`/`close`, `SEQ`, `STEPS_PER_REV`.
- **`hw::PlantBox`**: jordfugt via **I2C** @ `0x48`, pumpe via `pinctrl`, API: `setMinimumThreshold`, `setSkipWaterAtOrAbovePct`, `setPumpDurationSec`, `setHysteresisSec`, `getCurrentAverageMoisture`, m.fl.

På maskiner uden `libgpiod`: konfigurer med `-DOPTIHUS_MILJOESTYRING_HARDWARE=OFF` (stub-telemetry).

## `set_config` fra RPi1

Ud over `profile_name`, `profile_names`, `interval_sec`, `emergency_stop` kan serveren sende:

- **`bounds`**: `min_humidity_air`, `max_humidity_air`, `min_humidity_soil`, `max_humidity_soil` (aggregeret fra profiler). Bruges til **luftfugtighed** på motoren (vindue). **Jordvanding på `PlantBox` styres ikke længere ud fra disse jordfelter** i v1.
- **`temp_bounds_c`**: `min` / `max` for vinduesmotor (foreløbig standard 12 til 35 grader Celsius, indtil profiler får temperaturfelter)
- **`plant_boxes`**: array af plantekasseprofiler med bl.a. `id`, `label`, `slot`, `is_active`, `min_soil_pct`, `max_soil_pct`, `pump_duration_sec`, `hysteresis_sec`.

**Profiler:** Afgrøde-profiler (fra databasens `profiles`) aggregeres til `bounds` og bruges her til luft og vindue. Plantekasseprofiler i `plant_boxes` er et separat sæt parametre til jord/pumpe på Pi 2 og følger egne aktiveringsregler nedenfor.

### Plantekasse v1 (jord / pumpe)

**Officielle regler:**

- **0 aktive** profiler i `plant_boxes` betyder **ingen vanding**.
- **Præcis 1 aktiv** betyder at vanding følger **kun** den aktive profils `min_soil_pct`, `max_soil_pct`, `pump_duration_sec`, `hysteresis_sec`.
- **Flere end én aktiv** betyder **ingen vanding** (fail-safe) og tydelig log.

Internt sættes i fail-safe-tilstande en **teknisk stop** for pumpe (tærskler der gør betingelsen for vanding falsk); det er **ikke** en erstatning for reglerne ovenfor, kun en implementeringsdetalje i `PlantBox`/`control_manager`.

## Flere plantekasser (fremtid)

Anbefaling: **én JSON-linje pr. `Telemetry`-besked** (som nu). Ved flere kasser: enten **flere linjer i samme tidsinterval** (evt. med ekstra felt `box_id` i payload, når RPi1/DB understøtter det), eller ét udvidet payload. Vælg én model og hold den synkron med `telemetry_from_json` / database.

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

## Dybere forklaring (dansk)

Hele systemet, herunder hvordan felt-loops, sensorer og `set_config` hænger sammen, er beskrevet **simpelt og kritisk** i **`docs/optihus-fuld-forklaring-dansk.md`**.
