# Datahaandtering mappe

Denne mappe er server-delen af Optihus.

## Hvad den gør

- Starter TCP-server på port `5000`
- Starter Web ui på port `8080`
- Gemmer data i SQLite
- Behandler beskeder fra klienten
- TUI kan køres lokalt som fallback/admin

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
- `Web ui listening on port 8080`

## Fallback/admin TUI

Hvis Web ui ikke er tilgængelig, kan TUI startes direkte:

```bash
./build/datahaandtering/datahaandtering --tui
```

TUI er fallback/admin og erstatter ikke Web ui som primær brugerflade.

## Plantekasser / `plant_boxes` (v1)

**Afgrøde-profiler** og **plantekasseprofiler** er to separate ting: afgrøde-profiler (tabellen `profiles`) beskriver luft/jord-grænser pr. afgrøde og hvad der sendes som aggregeret `bounds` til vind og luft. Plantekasseprofiler (`plant_boxes`) har egne felter til jord/pumpe/hysterese og styrer vanding på Pi 2 kun når præcis én er aktiv.

- Web-ui og database bruger tabellen `plant_boxes` og JSON-nøglen **`plant_boxes`** i `set_config` (ingen rename af disse tekniske navne).
- **Officiel vandingslogik på klienten (miljøstyring):**
  - **0 aktive** plantekasseprofiler (`is_active` false på alle) betyder **ingen vanding** (fail-safe).
  - **Præcis 1 aktiv** betyder kun den profils **`min_soil_pct`**, **`max_soil_pct`**, **`pump_duration_sec`** og **`hysteresis_sec`** styrer jord/pumpe på Pi 2; profilens aggregerede jordgrænser i `bounds` bruges **ikke** som fallback for vanding.
  - **Flere end én aktiv** betyder **ingen vanding** (fail-safe) og tydelig log på klienten.
- **Én aktiv ad gangen** håndhæves også i databasen ved gem (upsert afvises hvis en anden allerede er aktiv). Web-ui blokerer samme tilstand client-side; ved afvisning returnerer `POST /api/plant-boxes/upsert` JSON med `"ok":false` og feltet **`error`** (kort dansk forklaring).
- Pi 2 bruger i dag kun én fysisk kasse (slot 1); øvrige rækker er konfigurationsdata til senere multi-kasse.

## Dybere forklaring (dansk, ikke-teknisk venlig)

Se **`vejledning/optihus-fuld-forklaring-dansk.md`** i repo-roden: den gennemgår mapper, websiden, TCP, databasen, Grafana og hardware (pumpe, jordfugt, motor, luftfugt-temperatur-sensor) i et sprog, der også kan læses uden IT-baggrund.
