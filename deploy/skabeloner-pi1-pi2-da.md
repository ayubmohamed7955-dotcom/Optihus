# Skabeloner til Pi 1 og Pi 2 (systemd)

Efter `git pull`: brug filerne i `deploy/systemd/`. **Søg og erstat** pladsholderne nedenfor — så undgår du at gætte forkert.

## Pladsholdere (begge enheder har eget Linux-login)

| Pladsholder | Hvor | Eksempel |
|-------------|------|----------|
| `__RPI1_LINUX_BRUGERNAVN__` | Kun **Pi 1**-servicefilen: `User=` og stier | `pi` eller `au756666` |
| `__RPI2_LINUX_BRUGERNAVN__` | Kun **Pi 2**-servicefilen: `User=` og stier | fx `pi` (kan være anderledes end på Pi 1) |
| **`server_host`** i JSON | Ikke i `.service` — se repo-rod **`miljoestyring_config.json.example`** | Fx `192.168.4.1` hvis Pi 1 er hotspot; ellers Pi 1’s LAN-IP |

**Vigtigt:** Linux-brugernavnet på Pi 1 og IP-adressen til Pi 1 er **to forskellige ting**. Klienten finder serveren via **`server_host`** i JSON — ikke via brugernavn.

Konfig-skabelonen ligger **i repo-roden**: `miljoestyring_config.json.example` → kopiér til `~/Optihus/miljoestyring_config.json` på Pi 2 og justér **`server_host`** efter behov (samme værdi som på din Windows-test, hvis netværket er ens).

## Pi 1 — datahåndtering

1. Erstat **alle** forekomster af `__RPI1_LINUX_BRUGERNAVN__` i  
   `deploy/systemd/skabelon-rpi1-datahaandtering.service`  
   med brugernavnet du logger ind med på Pi 1 (`whoami` på Pi 1).
2. Kopiér filen til systemd (uden «skabelon» i navnet):

   ```bash
   sudo cp ~/Optihus/deploy/systemd/skabelon-rpi1-datahaandtering.service /etc/systemd/system/optihus-datahaandtering.service
   ```

3. `sudo systemctl daemon-reload`
4. `sudo systemctl enable --now optihus-datahaandtering.service`

Sørg for at **`/opt/optihus/data/optihus.db`** findes og at `User=` må skrive til den.

## Pi 2 — miljøstyring

1. Erstat **alle** forekomster af `__RPI2_LINUX_BRUGERNAVN__` i  
   `deploy/systemd/skabelon-rpi2-miljoestyring.service`  
   med `whoami` på **Pi 2** (behøver ikke være samme navn som på Pi 1).
2. Opret `~/Optihus/miljoestyring_config.json` ved at kopiere **repo-roden** `miljoestyring_config.json.example` og justére **`server_host`** til Pi 1’s adresse (samme som når du tester med Windows/LAN).
3. Kopiér service:

   ```bash
   sudo cp ~/Optihus/deploy/systemd/skabelon-rpi2-miljoestyring.service /etc/systemd/system/optihus-miljoestyring.service
   ```

4. `sudo systemctl daemon-reload`
5. `sudo systemctl enable --now optihus-miljoestyring.service`

---

Se også [`boot-og-netvaerk-da.md`](boot-og-netvaerk-da.md) for hotspot / Wi‑Fi og baggrund.
