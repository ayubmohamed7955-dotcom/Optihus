# Opsætning: Raspberry Pi 1 – datahåndtering (central server)

Denne guide forudsætter, at Optihus-koden allerede ligger på Pi’en (fx i `~/Optihus`). Her står kun **hvad du gør på Pi 1**, i rækkefølge.

---

## Kort overblik – hvad skal Pi 1 kunne?

1. Nyeste Raspberry Pi OS (eller lignende Debian-baseret), netværkskonfigureret til **stabilt netværk** (ofte Wi‑Fi‑**accesspunkt/AP** eller fast IP på lokalt net).
2. Installere **byggeværktøj** og **SQLite**‑udviklingsbiblioteker + **JSON** til CMake.
3. Oprette **database-mappe** og rettigheder (sti i koden: `/opt/optihus/data/`).
4. **Bygge** programmet fra repo‑roden.
5. Sikre **arbejdskatalog ved start**: programmet finder `sql/init.sql` **relativt til den mappe**, du starter det fra – typisk repo‑roden.
6. (Valgfrit) **Automatisk start** ved boot med systemd.
7. Starte **`datahaandtering`**: TCP **5000**, Web UI **8080**, SQLite til telemetri/konfiguration.

---

## Forudsætninger på Pi 1

- Pi med strømforsyning, SD‑kort, netværksradio (Wi‑Fi til AP eller ethernet til router).
- Du har installeret Raspberry Pi OS og kan logge ind med SSH eller lokalt keyboard/skjerm.

---

### 1. Opdater pakker og installér afhængigheder til build + SQLite

```bash
sudo apt update
sudo apt install -y cmake ninja-build pkg-config build-essential \
    libsqlite3-dev nlohmann-json3-dev
```

**Hvorfor?** Projektets `CMakeLists` linker mod **SQLite3** og bruger **nlohmann_json**. Uden `-dev`-pakker finder CMake ikke biblioteker/headers ved konfiguration.

---

### 2. Opret mappe til SQLite-database og ejerskab til den bruger, der skal køre serveren

Koden bruger databasesti: **`/opt/optihus/data/optihus.db`** (konstant `kDbPath` i `shared/constants.hpp`). Opret første gang:

```bash
sudo mkdir -p /opt/optihus/data
sudo chown "$USER:$USER" /opt/optihus/data
sudo chmod u+rwX /opt/optihus/data
```

Hvis du senere bruger systemd med `User=pi`, skal **`pi`** kunne skrive her – udskift `chown til den bruger, som kører servicen.

**Hvorfor?** Manglende mappe/skriveadgang → `Could not open database` ved start. Projektet forventer en fast produktsti under `/opt/...`; det adskiller “driftsdata” fra kildekodemappen.

---

### 3. Byg optihus (fra repo-roden – tilpas stien til din mappe)

```bash
cd ~/Optihus          # eller den sti hvor dit repo allerede ligger
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target datahaandtering
```

Tjek at binary findes:

```bash
ls -la build/datahaandtering/datahaandtering
```

**Hvorfor?** Ét repo bygger både `optihus_shared`, `datahaandtering` og (hvis aktivt) `miljoestyring`. Du behøver kun target **`datahaandtering`** på Pi 1. **Ninja** er valgfrit; du kan bruge `cmake --build build` uden `-G Ninja` (så default-generator).

---

### 4. Før første test: netværksadgang på Pi 1

Serveren binder TCP til **alle interfaces** på port **5000** og Web UI på **8080**. Din Pi skal derfor have en **IP**, som klienter (Pi 2, laptop) kan nå – typisk:

- **AP-mode (demo-netværk):** Pi fungerer som Wi‑Fi-hotspot og har ofte **`192.168.4.1`** mod tilsluttede enheder. Det er ikke en del af C++‑koden – det sættes i OS med (fx) Raspberry Pi‑indstillinger, **NetworkManager** eller **`hostapd`+`dnsmasq`**. Konfigurationsdetaljer følger din valgte hotspot‑guide på nettet/OS-version.
- **Hjem på LAN/Router:** Slå DHCP‑reservation eller statisk IPv4 på din router til Pi 1, så **`miljoestyring_config.json` på Pi 2** matcher den IP på port **5000**.

**Hvorfor?** Uden korrekt netværksrouting ser Pi 2 aldrig `Listening on ... 5000`, og laptops når ikke `http://<pi-ip>:8080`.

---

### 5. Start datahåndtering manuelt (før systemd)

Du **skal starte eksekverbar fra repo-roden**, så `sql/init.sql` findes relativt til nuværende mappe (som programmerne forventer i default-flow):

```bash
cd ~/Optihus
./build/datahaandtering/datahaandtering
```

Forventede log‑temaer:

- Listening på TCP **5000**
- Browser UI på **8080**

TUI-fallback/admin (ingen Web UI på browser):

```bash
cd ~/Optihus
./build/datahaandtering/datahaandtering --tui
```

**Hvorfor?** Teknisk årsag til `WorkingDirectory` i systemd‑eksemplet (`vejledning/systemd/optihus-datahaandtering.service.example`) er netop `sql/` og relative stier ved schema‑kickstart.

Stop med **Ctrl+C** i terminalen testvis.

---

### 6. (Valgfrit) Systemd‑service ved boot

Kopier og redigér **`vejledning/systemd/optihus-datahaandtering.service.example`**:

- `User=` / `Group=` skal matche kontobruden der ejer **`/opt/optihus/data`**.
- `ExecStart=` og `WorkingDirectory=` skal pege på dit **rigtige repo**‑sti (som i eksemplet `/home/pi/Optihus`).

Eksempel efter kopier til `/etc/systemd/system/`:

```bash
sudo nano /etc/systemd/system/optihus-datahaandtering.service
sudo systemctl daemon-reload
sudo systemctl enable --now optihus-datahaandtering.service
sudo systemctl status optihus-datahaandtering.service
```

**Hvorfor?** Sikrer at server/Web UI/database kommer automatisk tilbage efter strømafbrud eller genstart med korrekt katalog til `sql/` og konsistent SQLite‑sti.

---

## Kort hjælp hvis det fejler

| Symptom | Tjek først |
|--------|-------------|
| `Could not open database` | Mapperettighed på `/opt/optihus/data` og brugerens ejerskab |
| Browser når ikke UI | Firewall eller forkert Pi‑IP; port **8080** og at processen kører |
| Pi 2 forbinder ikke | Pi 2’s `server_host` vs. Pi 1’s faktiske IP; port **5000** listen |
| Manglende `sql/init.sql`‑advarsel | Start altid fra repo‑rod eller sæt `WorkingDirectory=` i systemd dertil |

---

## Samlet forklaring om Pi 1’s rolle i systemet

**Datahåndtering** samler:

- Ét **SQLite**‑lager for telemetri, profiler, kommando­historik, plantekasser m.m.
- **TCP-server** på **5000**, som **`miljoestyring`** (Pi 2) taler til.
- **Minimal HTTP-Web UI/API** på **8080**, så opsættes uden ekspert-VNC på Pi.

Accesspunktet og Wi‑Fi‑navn/kodeord er primært et **Linux-netværksvalg**. De **standardiserede strenge** for SSID/password i kilden (`Hotspot`-konstanter i `shared/constants.hpp`) er til reference/dokumentation – den faktiske AP opsættes i OS eller routere uden om programmet selv starter AP’et automatisk fra C++‑koden i den beskrevne standardopsætning.

---

*Næste trin på felt-Pi:* se **`vejledning/setup-pi2-miljoestyring-da.md`.*
