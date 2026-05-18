# Miljøstyring (RPi2-klient)

Klientdelen af Optihus: TCP-mod `datahaandtering`, JSON-linjer som i `MessageEnvelope`.

**Mappe til headers:** `miljoestyring/include/` (f.eks. `app.hpp`, `tcp_client.hpp`). Under `include/hw/Watering system/` ligger `PlantBox.hpp` / `PlantBox.cpp` (jordfugt + pumpe); under `include/hw/Ventilation system/` ligger `DHT22.*` og `Motor.hpp` / `Motor.cpp` (luft temp/fugt via IIO + stepper‑vindue); tilsvarende under `src/hw/`.

## Hvad den gør

- Forbinder til server (standard `192.168.4.1:5000` eller `miljoestyring_config.json`)
- Sender `Hello` efter connect
- **Læser** indkommende `Command` (særligt `set_config`, `emergency_stop`, `clear_emergency_stop`) i baggrundstråd
- Sender `Telemetry` med jævne mellemrum (`interval_sec` fra server, default **900 s** hvis ikke sat)
- Reconnect ved fejl uden at vente på næste telemetri-interval
- På Linux: periodisk Wi-Fi recovery under længere disconnect (`nmcli radio wifi on` + `nmcli dev wifi rescan`)

## Hardware (Linux / Raspberry Pi)

CMake-option **`OPTIHUS_MILJOESTYRING_HARDWARE`** (default **ON** på Linux, **OFF** på Windows):

Link som partner‑zip: **`gpiod`** / **`gpiodcxx`** og **`Threads`** (`CMakeLists.txt` i zip bruger `find_package(Threads)` og linker mod samme biblioteker). Vi lister kildefiler eksplicit i stedet for `file(GLOB_RECURSE ...)`, hvilket er mere stabilt til builds.

Der findes **ingen** separat samlet “aktuator manager”‑klasse i buildet; pumpe og vindue håndteres direkte af **`PlantBox`** og **`Motor`** i `control_manager`‑flowet.

### GPIO og startadfærd (som partner `main.cpp`)

Pin‑tal fra partnerens `main.cpp` er samlet i **`include/hw/board_pins.hpp`** (`MOTOR_PIN1` … `MOTOR_PIN4`, `BOX1_PUMP_PIN`, og `DHT22_GPIO_PIN` til DHT‑læsning).

Ved boot startes **`motor.start()` / `plant_box.start()`** med det samme, så **luft‑ og jord‑sensorer opdateres til telemetri** også før bruger har konfigureret via web‑UI. **Vinduesmotor og pumpe** kører kun når **`set_config`** fra server har **`emergency_stop: false`** (typisk efter profil er sendt fra UI); indtil da er **`setActuationEnabled(false)`** på begge enheder. **`clear_emergency_stop`** genaktiverer kun aktuatorer hvis der tidligere er modtaget mindst ét **`set_config`** i denne proces (efter Pi‑genstart skal serveren derfor sende konfiguration igen, medmindre I senere tilføjer lokal persistens).

Partner‑zip’ens **`Logger`** er ikke porteret; den blev brugt til manuel test. Optihus bruger **`shared/logger`** til logning.

### DHT22 og Raspberry Pi firmware (`config.txt`)

Det **kan være vigtigt**, afhængigt af hvordan I læser sensoren:

- Partner‑README beskriver Device Tree Overlay så kernen eksponerer DHT via **IIO sysfs** (`dtoverlay=dht11,gpiopin=N`). På nyere Raspberry Pi OS ligger filen ofte som **`/boot/firmware/config.txt`** (ældre installationer kan have **`/boot/config.txt`**).
- **GPIO‑nummeret `N` skal matche den faktiske ledning *og* den konstant der bruges i software.** I repo’et er **`DHT22_GPIO_PIN`** defineret i **`board_pins.hpp`** og bruges i **`Motor::runLoop`** — hold overlay og denne værdi synkroniseret.
- Partner‑teksten brugte eksempelvis GPIO **17** i README, mens deres **`Motor.cpp`** brugte **24**; det er et klassisk “to kilder”-mismatch — ret efter jeres rigtige wiring.
- Hvis I **ikke** bruger overlay/IIO og læser sensoren anderledes, er `config.txt`‑trinnet irrelevant for den sti (men så skal `DHT22.cpp`‑læsningen også matche den metode).

- **`DHT22`** (`DHT22.hpp` / `DHT22.cpp` i **Ventilation system**): luft temp/fugt (IIO sysfs, som i partner‑zip) — bruges af **`Motor`** (`DHT22 sensor(...)` i `runLoop`).
- **`Motor`** (`Motor.hpp` / `Motor.cpp`): stepper + klima‑regulering (vinduet), samme kernelogik som partner‑zip’s `Motor`.
- **`PlantBox`** (`PlantBox.hpp` / `PlantBox.cpp`): jordfugt via **I2C** @ `0x48`, pumpe via `pinctrl`, samme kernelogik som partner‑zip’s `PlantBox` — API blandt andet `setMinimumThreshold`, `getCurrentAverageMoisture`; `max_soil_pct`, `pump_duration_sec` og `hysteresis_sec` fra server styrer **ikke** pumpecyklussen (kun `min_soil_pct`/`setMinimumThreshold` ved denne hardwareprofil).

På maskiner uden `libgpiod`: konfigurer med `-DOPTIHUS_MILJOESTYRING_HARDWARE=OFF` (stub-telemetry).

## `set_config` fra RPi1

Ud over `profile_name`, `profile_names`, `interval_sec`, `emergency_stop` kan serveren sende:

- **`bounds`**: `min_humidity_air`, `max_humidity_air`, `min_humidity_soil`, `max_humidity_soil` (aggregeret fra profiler). Bruges til **luftfugtighed** på vindues‑**ventilation** (`Motor`). **Jordvanding via `PlantBox` bruger ikke disse jordfelter** her.
- **`temp_bounds_c`**: `min` / `max` for vinduesmotor (foreløbig standard 12 til 35 grader Celsius, indtil profiler får temperaturfelter)
- **`plant_boxes`**: array af plantekasseprofiler med bl.a. `id`, `label`, `slot`, `is_active`, `min_soil_pct`, `max_soil_pct`, `pump_duration_sec`, `hysteresis_sec`.

**Profiler:** Afgrøde-profiler (fra databasens `profiles`) aggregeres til `bounds` og bruges her til luft og vindue. Plantekasseprofiler i `plant_boxes` er et separat sæt parametre til jord/pumpe på Pi 2 og følger egne aktiveringsregler nedenfor.

### Plantekasse v1 (jord / pumpe)

**Officielle regler:**

- **0 aktive** profiler i `plant_boxes` betyder **ingen vanding** (felt-hardware: via `setMinimumThreshold(100)`).
- **Præcis 1 aktiv** betyder at **tør-tærsklen** for vanding sættes fra profilens `min_soil_pct` (`setMinimumThreshold`). Felterne `max_soil_pct`, `pump_duration_sec` og `hysteresis_sec` findes i JSON og logges, men **styres ikke** af den nuværende partner-hardware `PlantBox` (fast pumpe‑/pauselogik i feltkoden).
- **Flere end én aktiv** betyder **ingen vanding** (fail-safe) og tydelig log.

Internt sættes i fail-safe-tilstande **høj tør-tærskel** på vanding (`setMinimumThreshold(100)`), så `PlantBox` ikke udløser pumpe; det er kun en felt-implementering i `PlantBox`/`control_manager`.

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

### Persistens af sidste feltkonfiguration (reboot)

Efter hver **`set_config`** gemmes parametrene atomisk på disk sammen med **`emergency_halt`** (overlay). Ved **`emergency_stop`** / **`clear_emergency_stop`** opdateres kun overlayet, hvis filen findes.

- **Linux (standard):** `/var/lib/optihus/miljoestyring_field_config.json` — mappen skal kunne oprettes/skrives af den bruger der kører feltnoden (evt. `sudo mkdir -p /var/lib/optihus && sudo chown …`).
- **Windows (udvikling):** `./optihus_state/` under proces­ens arbejdsmappe.
- **Override:** miljøvariabel **`OPTIHUS_STATE_DIR`** (sti til mappe).

Ved **opstart** indlæses filen før TCP‑forbindelse og anvendes som et syntetisk **`set_config`**, så grænser og aktuation kan genoprettes uden at vente på serveren.

## Byg

Fra repo-rod: `cmake -B build && cmake --build build --target miljoestyring`

## Dybere forklaring (dansk)

Hele systemet, herunder hvordan felt-loops, sensorer og `set_config` hænger sammen, er beskrevet **simpelt og kritisk** i **`vejledning/optihus-fuld-forklaring-dansk.md`**.
