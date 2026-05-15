# SQL mappe

Denne mappe indeholder database scripts til Optihus.

## Filer

- `init.sql` - opretter tabeller og indeks
- `seed_profiles.sql` - indsætter standard profiler

## Brug

Køres på server-node (Datahåndtering):

```bash
sqlite3 /opt/optihus/data/optihus.db < sql/init.sql
sqlite3 /opt/optihus/data/optihus.db < sql/seed_profiles.sql
```

## Formål

- Sikrer ens schema på tværs af installationer
- Gør setup reproducerbart for teamet

## Hvad tabellerne groft set er til

Ud over detaljer i `init.sql` kan du holde denne oversigt i hovedet (fuld forklaring på dansk: **`docs/optihus-fuld-forklaring-dansk.md`**):

- **telemetry**: målinger fra miljøstyring over tid (grundlag for grafer i Grafana).
- **profiles**: afgrødeprofiler med luft- og jordtal til server og vinduesmotor-delen af klienten. I skemaet findes feltet **`crop_type`** (tekst). I koden repræsenteres profilen i app-laget som `shared::Profile`, som kun har **`name`**. Ved database-upsert sættes `crop_type` bevidst til **samme værdi som `name`**, indtil der evt. introduceres en rigtig afgrøde-label separat fra det interne profilnavn — så undgår du misforståelser om “manglende” felt i den delte struct.
- **plant_boxes**: plantekasseprofiler med felter der har med vanding at gøre (v1-logik på klienten).
- **active_config**, **command_history**, **system_events**: gemt drift og sporbarhed.
