# Opsætning: Raspberry Pi 2 – miljøstyring (felt/klient)

Denne guide forudsætter, at Optihus-koden allerede ligger på Pi’en (fx i `~/Optihus`). Den beskriver kun **hvad du gør på Pi 2** i rækkefølge, så klienten forbinder til **Pi 1 (datahåndtering)** og – når aktiveret – bruger GPIO/I2C-hardware.

---

## Kort overblik – hvad skal Pi 2 kunne?

1. Raspberry Pi OS (Linux) på felt-Pi netværkskoblet (**samme net som Pi 1** eller på Pi 1’s Wi‑Fi‑AP).
2. Installere **byggeværktøj**, JSON-bibliotek og – **kun hvis GPIO/I2C build er ON** – **libgpiod** til motor/vinduet.
3. Tilslut **`miljoestyring_config.json`** så **`server_host` / `server_port`** matcher Pi 1.
4. (Anbefalet) Opdatere **`/boot/firmware/config.txt`** eller **`/boot/config.txt`** med korrekt **DHT-overlay** (**GPIO må matche** `board_pins.hpp`), hvis luft-/fugtemåling er tændt på den måde projekt forventer via IIO-sysfs (`DHT22.cpp`).
5. Opret **`/var/lib/optihus/`** eller sætte **`OPTIHUS_STATE_DIR`** så **sidste `set_config`** kan gemmes lokalt ved genstart (**persistens‑JSON**).
6. **Bygge** **`miljoestyring`** mål‑target ved hardware **ON**.
7. (Valgfrit) **systemd‑service**.
8. Køre **klient‑processen**, indtil logs viser succesfuldt **Hello**-/connect til Pi 1.

---

## Forudsætninger

- Pi 1 kører allerede `datahaandtering` og er netværksmessigt synlig på den IP/host du bruger på Pi 2.
- Du ved om feltet bruger reel **motor/pumpe-sensorhardware** eller en **kun-software-/stub‑build**.

---

### 1. Opdater pakker og installér build-afhængigheder (+ hardware hvis aktiv)

 Grundpakke alle Linux-builds på Pi:

```bash
sudo apt update
sudo apt install -y cmake ninja-build pkg-config build-essential nlohmann-json3-dev
```

**Med GPIO/I2C (standard på Linux‑build på Pi – `OPTIHUS_MILJOESTYRING_HARDWARE=ON`):**

```bash
sudo apt install -y libgpiod-dev
```

**Uden gpio (kun netværks-/dev-testing på Linux-maskine eller når CMake slår HW fra):**

- Kør CMake med `-DOPTIHUS_MILJOESTYRING_HARDWARE=OFF`.

**Hvorfor?** `miljoestyring` linker **`gpiodcxx` / `gpiod`** ved ON for **Motor**/stepper. Uden pakken fejler link-trin på Pi. Ved OFF bygges stadig klient‑logik TCP/JSON uden aktuator/driverfil-kompilet.

---

### 2. (Kun reel hardware): Device Tree-overlay til luft-/fugt-sensor og GPIO-match

Åbn OS-konfiguration (sti afhænger af OS-version):

```bash
sudo nano /boot/firmware/config.txt
```

Hvis feltet ikke eksisterer, prøv `sudo nano /boot/config.txt`.

Et typisk udvalg ved DHT eksponeres som IIO-sysfs (**som i `DHT22.cpp`**), fx:

```ini
dtoverlay=dht11,gpiopin=24
```

Værdi **`gpiopin=`** **skal** matche **`DHT22_GPIO_PIN`** i `miljoestyring/include/hw/board_pins.hpp` (og den faktisk monterede gpio-ledning på boardet).

Reboot derefter:

```bash
sudo reboot
```

**Hvorfor?** Kernen eksponérer først målinger på `/sys/bus/iio/devices/...` **når** overlay er aktiv. Forkert PIN → tomme/læsnings­fejl; forkert dokumentation contra kode (**README bemærker** partner-historiske mismatch mellem README og hvad kode bruger) → fejldiagnose.

Motor-stepper-pin og pumpe GPIO findes også i **`board_pins.hpp`** til tilpasning før build.

---

### 3. Opret driftmappe til lokal felts persistens‑JSON på Linux

Miljøstyring gemmer **sidste succesfuldt modtagede `set_config`** m.m. (**se `miljoestyring/README.md`**). Standard‑Linux‑sti kræver skriveadgang ved den bruger, der starter processen:

```bash
sudo mkdir -p /var/lib/optihus
sudo chown "$USER:$USER" /var/lib/optihus
sudo chmod u+rwX /var/lib/optihus
```

Alternativ placering ved behov før start (`export`-linje før manuel `./miljoestyring` eller `[Service]`‑miljø i systemd):

```bash
export OPTIHUS_STATE_DIR=/sti/til/mit/state
```

**Hvorfor?** Udover netværksreplay fra Pi 1 ønskes **auto-start efter blackout** også uden øjeblikkelig nedhentning fra serveren første sekund ved boot.

---

### 4. Tilpas `miljoestyring_config.json` i repo-roden (server IP og port klient må forbinde til)

Eksempel (kopier også fra repo´s `miljoestyring_config.json.example` ved siden af din build):

Fil skal ligge **`miljoestyring_config.json`** i **nuværende arbejdsmappe når klient starter** (`WorkingDirectory`). Standard i eksempel:

```json
{
  "server_host": "192.168.4.1",
  "server_port": 5000
}
```

Udskift `server_host` med Pi 1’s **faktisk rutebare IPv4 eller DNS-navn**.

**Hvorfor?** `app.cpp` læser filen før connect-loop. Fejl eller manglende fil → fallback til baked standard i kode (typisk også `192.168.4.1`/`5000` men dokumentér altid eksplicit LAN).

---

### 5. Byg miljøstyring fra repo‑roden

```bash
cd ~/Optihus               # eller din repo-sti
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target miljoestyring
```

Eksplicit slå gpio fra ved behov ved reconfigure før build:

```bash
cmake -S . -B build -DOPTIHUS_MILJOESTYRING_HARDWARE=OFF
cmake --build build --target miljoestyring
```

**Hvorfor?** Ved ON kompiler-es **Motor**, **PlantBox**, **DHT22**‑fil ind i binary med libgpiod. Ved OFF forbliver stubs i telemetriløkker uden fysisk jordmotor.

---

### 6. Manuel testkør før systemd – altid korrekt arbejdskatalog

```bash
cd ~/Optihus
./build/miljoestyring/miljoestyring
```

Forventet log tema når sund forbindelse: **Connecting / Hello**/Connected til server **host:port**.

Stop med Ctrl+C ved testvis.

Valgfrie miljø til persistens før start:

```bash
export OPTIHUS_STATE_DIR=/var/lib/optihus
./build/miljoestyring/miljoestyring
```

**Hvorfor:** `miljoestyring_config.json`-opslag relativt cwd; systemd skal også sætte `WorkingDirectory` til repo eller mappe hvor `.json`-filen kopieret.

---

### 7. (Valgfrit) Systemd automatisk ved boot

Redigér-kopi af **`vejledning/systemd/optihus-miljoestyring.service.example`**:

- `WorkingDirectory=` **samme repo**‑rod som **`miljoestyring_config.json`**
- Tilføj ved behov **`Environment=OPTIHUS_STATE_DIR=/var/lib/optihus`**
- `User=` må skrive **`/var/lib/optihus`**

```bash
sudo cp ~/Optihus/vejledning/systemd/optihus-miljoestyring.service.example /etc/systemd/system/optihus-miljoestyring.service
sudo nano /etc/systemd/system/optihus-miljoestyring.service
sudo systemctl daemon-reload
sudo systemctl enable --now optihus-miljoestyring.service
sudo systemctl status optihus-miljoestyring.service
```

**Hvorfor?** Produktion på feltet må ikke stole på åben SSH-session for at genstart-process efter blackout.

---

## Kort hjælp hvis noget ikke virker på Pi 2

| Symptom | Tjek først |
|--------|-------------|
| `Could not connect` loop | **`server_host`**, Pi 1 kører, samme VLAN/subnet/AP, ikke brandmur-blok på 5000 |
| Build fejl `gpiod` | INSTALL **`libgpiod-dev`** eller sæt `OPTIHUS_MILJOESTYRING_HARDWARE=OFF` ved dev uden gpio |
| DHT/tom temperatur eller default stub | Overlay `config.txt` vs `DHT22_GPIO_PIN`/`board_pins.hpp` OG reboot |
| Vanding forbliver neutral | Aktiv **`plant_boxes`** politik (0 aktiv / >1 aktiv = fail-safe ingen vanding) – dokumenteret **`miljoestyring/README`** |
| Ingen lokale **`set_config` efter boot** før server | Persistensfiler kan ikke skrive (`/var/lib/optihus` eller `OPTIHUS_STATE_DIR`) |

---

## Samlet forklaring om Pi 2’s rolle

**Miljøstyring**:

- Åbner **udgående** TCP-session til **`datahaandtering`**, sender **telemetry**‑JSON-linjer, modtager kommando **`set_config`**, **`emergency_stop`**, **`clear_emergency_stop`**, mv.
- Kører **baggrundhardware**‑loops (**Motor vindue**/ **PlantBox jord**/ **DHT** luft‑læsningsgren) ved ON build.
- **Fail-safe før konfig**: aktuator-motion/pumpe begrænses indtil konsistent server **`set_config` uden aktiv nødstop** (**`README` dokumenter nu `setActuationEnabled`** arkitekturen).
- **Lokal persistent JSON til genstart‑replay**, så driftsprofiler overlever Raspberry Pi blackout uden øjeblikkelig nedhentningsafhængighed af Pi 1 (men netværksforbindende kommer før/parallelt for fuld konsistens med database på Pi 1).

For dybere baggrund (**Web UI Grafana reference** eller feltkomponentdiagram), se også **`vejledning/optihus-fuld-forklaring-dansk.md`**.

---

*Pi 1 trin først*: **`vejledning/setup-pi1-datahaandtering-da.md`**.
