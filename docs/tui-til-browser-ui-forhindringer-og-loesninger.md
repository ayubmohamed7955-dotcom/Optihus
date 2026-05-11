# TUI til Browser-UI: forhindringer og løste fejl

## Formål

Dette notat beskriver:

- de vigtigste forhindringer ved at flytte Optihus fra terminal-TUI til browser-UI
- hvilke konkrete fejl/problemer vi har løst i TUI/browser-flowet indtil nu
- hvilke risici der stadig skal håndteres for en robust aflevering

## Forhindringer ved konvertering fra TUI til browser-UI

### 1) Tilstandsmodel: fra enkeltprofil til multi-profil

Den oprindelige model var i praksis bygget omkring `profile_name` som én værdi.
Browser-UI krævede valg af flere profiler samtidig.

**Udfordring:** Eksisterende datafelt og dele af logikken forventede enkeltprofil.

**Løsning:** Vi understøtter nu flere profiler som kommasepareret liste i aktiv konfiguration, og eksponerer `profile_names` (array) for tydeligere håndtering i UI/API.

### 2) Validering: UI-valg er ikke nok alene

Kun browser-validering er ikke robust nok, fordi API kan kaldes direkte.

**Udfordring:** Ugyldige profiler kunne tidligere gemmes i aktiv konfiguration.

**Løsning:** Backend validerer nu:

- mindst én profil er valgt
- alle valgte profiler findes i databasen

### 3) Konsistens mellem TUI og browser-UI

Hvis TUI og browser-UI håndterer profiler forskelligt, får man uforudsigelig drift.

**Udfordring:** TUI havde enkeltprofil-flow, mens browser-UI fik multi-profil.

**Løsning:** TUI er opdateret til samme princip:

- valg af flere profiler (kommasepareret input)
- krav om mindst én profil
- validering mod profiler i database

### 4) Robusthed ved sletning af profiler

Sletning kan gøre aktiv konfiguration ugyldig, hvis slettet profil var valgt.

**Løsning:** Efter sletning justeres aktiv konfiguration automatisk:

- fjern den slettede profil fra valgt liste
- hvis listen bliver tom, vælges første resterende profil (hvis der er en)

### 5) Sprog og UX til danske brugere

Browser/TUI-tekster skulle være på dansk, men kode på engelsk.

**Løsning:** Brugerrettede tekster er gjort danske og mere præcise (fulde feltnavne, ikke forkortelser).

## Fejl/problemer vi har løst i TUI/browser-flowet

1. **Tom profil-liste blev accepteret i config-opdatering**
   - Nu afvises request med fejl: "at least one profile must be selected".

2. **Ikke-eksisterende profiler kunne gemmes i aktiv config**
   - Nu afvises request med fejl: "unknown profile: ...".

3. **Sletning af aktiv profil kunne efterlade tvetydig tilstand**
   - Nu normaliseres aktiv konfiguration efter profilsletning.

4. **Uens adfærd mellem UI-lag**
   - TUI følger nu samme multi-profil-regel som browser-UI.

5. **Uklare brugertekster**
   - Tekster er oversat til dansk med fulde beskrivelser (fx "Minimum luftfugtighed").

## Kritiske resterende risici

1. **End-to-end styringslogik i miljoestyring**
   - Selvom payload nu indeholder `profile_names`, skal runtime-logikken stadig være helt entydig i miljøstyringen (hvordan flere profiler omsættes til styringsbeslutninger i praksis).

2. **Dataformat på lang sigt**
   - Kommasepareret liste i ét felt er pragmatisk, men ikke ideel til kompleks fremtidig logik.
   - En normaliseret løsning (fx separat tabel for valgte profiler) vil være mere skalerbar.

3. **Manglende automatiske tests**
   - De nye regler er verificeret manuelt via API/UI.
   - Der mangler stadig automatiske tests for regression på profilvalg, profilsletning og config-sync.

## Anbefalet næste minimumstrin

1. Fastlåst regel i dokumentation: "første profil er primær".
2. Implementér/validér samme regel eksplicit i miljøstyringens anvendelse af config.
3. Tilføj 3-5 målrettede integrationstests for:
   - tom profil-liste
   - ukendt profilnavn
   - profilsletning med aktiv konfiguration

