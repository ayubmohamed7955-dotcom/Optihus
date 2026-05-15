# Raspberry Pi: start ved boot (systemd) og netværk (uden for programmet)

Kort guide: **Linux** starter dine programmer via **systemd** og **én lille `.service`-fil** pr. Pi. **Access point og Wi‑Fi-klient** sættes i **systemet** (NetworkManager e.l.) — ikke inde i C++-koden.

**Klar fyld-skabeloner (Pi 1 / Pi 2):** [`skabeloner-pi1-pi2-da.md`](skabeloner-pi1-pi2-da.md) og filerne under `deploy/systemd/skabelon-rpi*-*.service`.

## 0. Typisk mappe-layout (som efter `git clone` + `cmake` i `~/Optihus`)

Hvis dine binarer ligger som i projekt-træet:

- `~/Optihus/build/datahaandtering/datahaandtering`
- `~/Optihus/build/miljoestyring/miljoestyring`

…sæt i service-filerne **`ExecStart`** til disse stier.

**Pi 1 — vigtigt:** `datahaandtering` leder efter **`sql/init.sql` relativt til den mappe du starter processen fra** (`WorkingDirectory` i systemd skal derfor normalt være **repo-roden** `~/Optihus`, ikke `build/...`).

**Pi 2:** Læg **`miljoestyring_config.json`** i samme mappe som `WorkingDirectory` (fx `~/Optihus`). Kopiér skabelonen **`miljoestyring_config.json.example`** fra **repo-roden** og omdøb til `miljoestyring_config.json`; justér **`server_host`** til Pi 1’s adresse på det netværk Pi 2 bruger.

**Database:** Koden åbner stadig **`/opt/optihus/data/optihus.db`**. Den mappe og fil skal findes og være skrivbar for den bruger systemd kører servicen som (`User=`). Tilpas også `User=`/`Group=` hvis din login-bruger ikke hedder `pi` (fx `au756666`).

Erstat **`/home/pi/Optihus`** i eksemplerne med din egen sti, eller kør:

```bash
echo /home/$(whoami)/Optihus
```

## Fejlfinding (hvis `cp … No such file` eller systemd klager)

1. **`cp: cannot stat 'deploy/systemd/...'`**  
   Mappen **`deploy/`** findes ikke i din lokale clone (for gammel kode eller aldrig hentet). Kør **`git pull`** i `~/Optihus` og tjek derefter:

   ```bash
   ls ~/Optihus/deploy/systemd/
   ```

   Du bør fx se `optihus-datahaandtering.service.example` og `skabelon-rpi1-datahaandtering.service`.  
   Finder du dem ikke, er klientens repo ikke på samme niveau som `origin` — lav `git pull` eller kopier **`deploy/`** manuelt fra en maskine hvor den findes.

2. **`enable`/`start` klager over manglende `[Install]` / “bad unit file”**  
   De store fejl kommer tit af at **manuel redigering i `nano` har ødelagt filen**. Meget almindeligt: man har **kopieret hele terminal-blokken** (inkl. `sudo systemctl …`) **ind i filen**.  
   I **`/etc/systemd/system/optihus-….service`** må der **kun** stå systemd-syntaks (`[Unit]`, `ExecStart=…`, `[Install]` + `WantedBy=multi-user.target`, osv.) — **ikke** `sudo`-kommandoer.

   Nulstil og prøv igen:

   ```bash
   sudo rm -f /etc/systemd/system/optihus-datahaandtering.service
   sudo systemctl daemon-reload
   ```

   Kopier derefter `.service`-fil fra repo **kun med `cp`** (som nedenfor), og ret **kun** linjer som `User=`, `Group=`, `ExecStart=`, `WorkingDirectory=` i **`nano`** — luk nano, og **så først** kør `daemon-reload` og `enable` i terminalen.

## 1. Pi 1 — datahåndtering

### Access point (Wi‑Fi hotspot)

Programmet **starter ikke** AP selv. Du sætter et hotspot i OS, så Pi 1 fx får `192.168.4.1`, mens `datahaandtering` bare lytter på port **5000** og **8080**.

- På mange Raspberry Pi OS-installationer kan du bruge **NetworkManager** eller **GUI “Wireless AP”**.
- Sikr at forbindelsen har **Autoconnect / start ved boot** (ordlyden afhænger af værktøj).
- SSID/kodeord kan matche `shared/constants.hpp` (`OptihusNet` / …) — det er **kun** til Wi‑Fi; det er ikke det samme som systemd.

### Autostart af `datahaandtering`

0. **`git pull`** i `~/Optihus`, så du har **`deploy/systemd/`** (se afsnittet **Fejlfinding** øverst hvis `cp` ikke finder filer).

1. Sikr databasesti: **`/opt/optihus/data/optihus.db`** (opret mappe + `sqlite3 … < sql/init.sql` fra repo som i øvrige docs). Ret **ejer/rettigheder** så `User=` i din service må skrive.

2. Kopiér skabelonen (brug én linje fra **repo-roden** — virker uanset om du står i `~/Optihus` eller ej):

   ```bash
   sudo cp "$HOME/Optihus/deploy/systemd/optihus-datahaandtering.service.example" /etc/systemd/system/optihus-datahaandtering.service
   ```

   Alternativ (Pi 1 med pladsholdere):  
   `sudo cp "$HOME/Optihus/deploy/systemd/skabelon-rpi1-datahaandtering.service" /etc/systemd/system/optihus-datahaandtering.service`  
   og erstat **alle** `__RPI1_LINUX_BRUGERNAVN__` med output af `whoami` (fx `au756666`).

3. **Redigér kun unit-filen** med `sudo nano` (ikke bland terminal-kommandoer ind i filen):

   ```bash
   sudo nano /etc/systemd/system/optihus-datahaandtering.service
   ```

   Ret **`User=`**, **`Group=`**, **`ExecStart=`** og **`WorkingDirectory=`** så de matcher **dit** brugernavn og `~/Optihus` (eksemplet bruger `pi` og `/home/pi/Optihus` — skift til fx `au756666` og `/home/au756666/Optihus`). Gem og **luk nano** (`Ctrl+O`, Enter, `Ctrl+X`).

4. **Først når nano er lukket** — kør i terminalen:

   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable --now optihus-datahaandtering.service
   ```

5. Tjek: `systemctl status optihus-datahaandtering.service`

## 2. Pi 2 — miljøstyring

### Wi‑Fi som klient

Tilslut Pi 2 til Pi 1’s netværk (samme som når du tester manuelt). Det er en **gemt Wi‑Fi-forbindelse med autoconnect** i OS — ikke noget `miljoestyring` gør.

### Autostart af `miljoestyring`

1. Fra **repo-rod:** `cp miljoestyring_config.json.example miljoestyring_config.json` (i `~/Optihus`) og ret **`server_host`** hvis Pi 1 ikke er på `192.168.4.1`.
2. Kopiér unit-fil, ret i **nano** (kun systemd-linjer — se fejlfinding), og kør **derefter** `systemctl` i terminalen:

   ```bash
   sudo cp "$HOME/Optihus/deploy/systemd/optihus-miljoestyring.service.example" /etc/systemd/system/optihus-miljoestyring.service
   sudo nano /etc/systemd/system/optihus-miljoestyring.service
   ```

   Efter gem/luk nano:

   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable --now optihus-miljoestyring.service
   ```

## 3. Hvorfor det virker på Windows men “ikke af sig selv” på Pi

På Windows starter **du** processen i en terminal — der er ingen systemd. På Pi skal du **én gang** installere services som ovenfor; derefter starter programmet ved hver genstart uden manuel `cd` og `./datahaandtering`.

## 4. Filer i `deploy/systemd/`

- `optihus-datahaandtering.service.example` / `optihus-miljoestyring.service.example` — hurtige skabeloner (`User=pi` — ret til dit login).
- `skabelon-rpi1-datahaandtering.service` / `skabelon-rpi2-miljoestyring.service` — samme idé med pladsholder **`__RPI1_LINUX_BRUGERNAVN__`** / **`__RPI2_LINUX_BRUGERNAVN__`** (se `skabeloner-pi1-pi2-da.md`).

Kopiér til `/etc/systemd/system/optihus-….service` (uden `skabelon-` / `.example` i navnet) og ret stier.
