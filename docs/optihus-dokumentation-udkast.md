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

## 9) Rapportvinkel (kort)

I rapport/demo bør I tydeligt adskille:

- lokal proof-of-life (loopback)
- reel cross-device verifikation (to forskellige IP-adresser + `ESTABLISHED`)

Det styrker fagligheden i netværksdelen markant.

