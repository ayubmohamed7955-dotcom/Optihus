# Optihus - Hurtig guide (ikke-teknisk)

Denne guide er til teammedlemmer, der bare skal starte systemet og bekræfte at det virker.

## Formål

Få Optihus i drift med mindst mulig teknik:

- Datahåndtering kører som server
- Miljøstyring forbinder som klient
- Browser UI på datahåndtering er primær brugerflade
- Forbindelsen er OK når begge viser forventede linjer

## Før du starter

- Sørg for at begge Raspberry Pi er tændt.
- Sørg for at klient-enheden er på samme netværk/AP som serveren.
- AP-mode bruger normalt server-IP `192.168.4.1`.

## Start server (Datahåndtering)

På server-enheden:

```bash
cd ~/Optihus
./build/datahaandtering/datahaandtering
```

Det er korrekt når du ser:

- `Listening on port 5000`

## Start klient (Miljøstyring)

På klient-enheden:

```bash
cd ~/Optihus
./build/miljoestyring/miljoestyring
```

På Windows-klient:

```powershell
cd "C:\Users\lasis\Optihus"
.\build-windows-tcp\miljoestyring\miljoestyring.exe
```

Det er korrekt når du ser:

- `Connected and sent hello ...`

## Hvad betyder "det virker"?

Systemet virker når:

1. server lytter på port `5000`
2. klient forbinder til serverens IP
3. server logger klientens IP
4. browser UI åbner på `http://192.168.4.1:8080` fra laptop på AP

## Interface-regler

- Browser UI bruges til normal status og kontrol.
- TUI bruges kun som fallback/admin.
- Grafana bruges kun til read-only grafer/overblik (ikke kontrol).

## Fallback hvis UI ikke åbner automatisk

1. Åbn browser manuelt og gå til `http://192.168.4.1:8080`.
2. Hvis browser UI stadig ikke kan bruges, kør fallback/admin TUI på Pi 1:

```bash
./build/datahaandtering/datahaandtering --tui
```

## Stop systemet

- Tryk `Ctrl + C` i terminalen på hver enhed.

## Hvis noget fejler

- Tjek først at klientens `server_host` er serverens rigtige IP (ikke `127.0.0.1`).
- Tjek at serveren stadig kører.
- Start klienten igen.
