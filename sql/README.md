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
