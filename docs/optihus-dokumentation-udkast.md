# Optihus - Dokumentationsudkast

Dette dokument er et praktisk udkast til rapport og drift for Optihus.
Fokus er stabil drift, tydelig rollefordeling og enkel fejlsøgning.

## 1) Projekt og mål

Optihus består af to noder:

- **Datahåndtering (server/AP-node)**: TCP-server, database og central styring.
- **Miljøstyring (klient-node)**: TCP-klient og kontrol-loop.

Målet er lokal, robust kommunikation mellem noderne uden cloud-afhængighed.

## 2) Kommunikationsmodel

- Transport: TCP
- Port: `5000`
- Protokol: JSON-lines (én JSON-besked pr. linje)
- Normal handshake:
  1. klient forbinder
  2. klient sender `hello`
  3. server svarer med aktiv konfiguration

## 3) Valideret netværkssetup

Primær driftsmode er AP på serveren:

- Server/AP IP: `192.168.4.1`
- Klient får AP-adresse i `192.168.4.x`

Cross-device test er valideret med:

- server-log med reel klient-IP (ikke loopback)
- klient-log med connect til reel server-IP
- `ESTABLISHED` socket-state mellem to forskellige hosts

## 4) Build og kørsel

### Raspberry Pi

```bash
cmake -S . -B build
cmake --build build -j4
```

Server:

```bash
./build/datahaandtering/datahaandtering
```

Klient:

```bash
./build/miljoestyring/miljoestyring
```

### Windows (miljøstyring)

```powershell
cmake -S . -B build-windows-tcp -G Ninja -DOPTIHUS_BUILD_DATAHAANDTERING=OFF -DOPTIHUS_BUILD_MILJOESTYRING=ON
cmake --build build-windows-tcp -j4
.\build-windows-tcp\miljoestyring\miljoestyring.exe
```

## 5) Konfigurationsregel for AP-mode

På klienten skal `miljoestyring_config.json` pege på serverens AP IP:

```json
{
  "server_host": "192.168.4.1",
  "server_port": 5000
}
```

Hvis `127.0.0.1` bruges, bliver testen kun lokal loopback og ikke cross-device.

## 6) Boot-reliability princip

Brugeroplevelsen skal være startup-order tolerant:

- Noder må gerne starte i vilkårlig rækkefølge.
- Systemet skal selv komme i stabil forbindelse uden manuel procesrestart.
- Dette verificeres med faste acceptkriterier i integrationstest.

## 7) Acceptance criteria (integration)

En test anses som bestået når alle punkter er opfyldt:

- server lytter på `:5000`
- klient forbinder til reel server-IP
- server logger reel klient-IP
- socket viser `ESTABLISHED` mellem to forskellige hosts
- ingen manuel restart nødvendig i normal startup

## 8) Kort fejlsøgningsguide

### `connect() failed: 10061` (Windows klient)

Typisk årsag: forkert `server_host` eller server ikke aktiv.

Tjek:

1. Kører `datahaandtering`?
2. Er klient sat til server-IP (fx `192.168.4.1`)?
3. Er porten stadig `5000`?

### Forbindelse opstår ikke i AP-mode

Tjek:

- klient er koblet på AP
- klient har `192.168.4.x` adresse
- server lytter (`ss -ltnp | grep 5000`)

## 9) Drift: skift mellem AP og almindelig Wi-Fi

Brug dette når server-Pi midlertidigt skal væk fra hotspot og på et normalt Wi-Fi.

Skift **fra AP til normal Wi-Fi**:

```bash
sudo nmcli con down Optihus-AP
nmcli dev wifi rescan
nmcli dev wifi list
sudo nmcli con up WIFI-WIFI-WIFI
```

Skift **tilbage til AP**:

```bash
sudo nmcli con down WIFI-WIFI-WIFI
sudo nmcli con up Optihus-AP
```

Verifikation:

```bash
nmcli dev status
ip addr show wlan0
ip route
```

## 10) Brugermanual (slutbruger)

Denne del er til en bruger uden teknisk baggrund.

### Daglig brug

1. Tænd begge Raspberry Pi-enheder.
2. Vent 1-2 minutter.
3. Forbind din PC/laptop til Wi-Fi: `Optihus`.
4. Start klient-/styringsprogrammet (hvis det ikke starter automatisk).
5. Kontroller at systemet viser normal status.

### Normal forventning

- Systemet holder forbindelsen automatisk.
- Målinger opdateres løbende.
- Ved kort netværksafbrydelse forsøger systemet at genoprette forbindelsen.

### Hvis noget ikke virker

1. Genstart først klienten.
2. Hvis stadig fejl: genstart begge Pi’er.
3. Hvis stadig fejl: kontakt en developer og oplys:
   - tidspunkt for fejl
   - hvad du gjorde lige før fejlen
   - om Wi-Fi `Optihus` var synligt

## 11) Developer-manual (team/kollegaer)

### Hurtig driftstjek

På server:

```bash
ss -ltnp | grep 5000
```

På klient:

```bash
ip addr show wlan0
ping -c 4 192.168.4.1
```

### Standard startsekvens

Server:

```bash
./build/datahaandtering/datahaandtering
```

Klient:

```bash
./build/miljoestyring/miljoestyring
```

### Log-signaler der bør ses

- Server:
  - `Listening on port 5000`
  - `Client connected: <ip>`
- Klient:
  - `Starting field node`
  - `Connected and sent hello`

### Typiske fejl og hurtig handling

- `Could not connect to server`:
  - tjek `miljoestyring_config.json`
  - tjek at serveren kører
  - tjek AP-tilslutning
- `Dropped invalid JSON line` på server:
  - tjek JSON-format, `type`, `source`, `ts` og newline-delimiter.

### Team-regel

- Ingen ændring af message-felter uden fælles kontrakt-opdatering.
- Ingen hardcoding af server-IP i source (brug config).

## 12) Rapportvinkel (kort)

I rapport/demo bør I tydeligt adskille:

- lokal proof-of-life (loopback)
- reel cross-device verifikation (to forskellige IP-adresser + `ESTABLISHED`)

Det styrker fagligheden i netværksdelen markant.

