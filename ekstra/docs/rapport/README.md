# Optihus — rapportpakke

Samler **rapportmateriale** til systembeskrivelsen (Pi 1: `datahaandtering`, Pi 2: `miljoestyring`).

| Fil / mappe | Formål |
|-------------|--------|
| [RAPPORT.md](RAPPORT.md) | Hoveddokument (tekst, referencer til repo) |
| [diagrams/optihus-system.puml](diagrams/optihus-system.puml) | PlantUML‑diagrammer (én fil, mange `@startuml`‑blokke) |

Miljøstyring forbinder som standard til datahåndtering på access‑point‑adressen **`192.168.4.1`** og TCP‑port **`5000`** (se `shared/include/shared/constants.hpp`). Pi 2 kan override host/port via **`miljoestyring_config.json`** i arbejdsmappen (kopier fra **`miljoestyring_config.json.example`** i repo‑roden).

## Eksporter diagrammer (PNG/SVG)

**Online:** [plantuml.com](https://www.plantuml.com/plantuml/uml/) — indsæt én `@startuml`…`@enduml`‑blok ad gangen, eller upload filen.

**Lokalt** (Java + [PlantUML](https://plantuml.com/) installeret):

```bash
plantuml docs/rapport/diagrams/optihus-system.puml
```

Typisk oprettes **én billedfil pr. diagram-id** (fx `optihus-context.png`).

## Vedligeholdelse

- Databasefelter i ER‑diagrammet skal følge [sql/init.sql](../../sql/init.sql) og eventuelle migrationer i `datahaandtering/src/database_manager.cpp`.
- Hold **PlantUML** (`optihus-system.puml`) synkron med felt persistens (`set_config_store`), telemetri **`field_live`** / **`field_node`**, **`board_pins.hpp`**, og hardware‑mapperne under `miljoestyring/include/hw/`.
- Systemd‑skabeloner: [vejledning/systemd](../../vejledning/systemd).
