# Struktur for Markdown-filer

Denne fil forklarer, hvor dokumentationen ligger, og hvorfor.

## Princip

Hver hovedmappe har en kort `README.md`, som forklarer:

- hvad mappen bruges til
- hvilke filer der er vigtigst
- hvordan den bruges i praksis

## Placering

- `README.md` (repo-rod)
  - overblik over hele projektet
- `datahaandtering/README.md`
  - serverdel
- `miljoestyring/README.md`
  - klientdel
- `shared/README.md`
  - fælles kode
- `sql/README.md`
  - database scripts
- `vejledning/*.md` (tidligere `docs/` + systemd og Windows-guide)
  - tværgående guides, arkitektur, test og rapport, RPi systemd, Windows-build

## Hvorfor denne struktur hjælper

- Nye teammedlemmer kan finde rundt hurtigere
- Mindre afhængighed af mundtlig viden
- Nemmere for vejleder/sensor at forstå projektets opbygning

## Vedligeholdelsesregel

Når en mappe ændres væsentligt, opdateres dens `README.md` i samme commit.
