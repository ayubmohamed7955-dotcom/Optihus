# Byg på Windows (datahåndtering og/eller miljøstyring)

Kort opskrift til egen CMake-build-mappe. Bygmapper i repo-roden (.gitignored) ved du selv hvad du vil kalde fx `build-windows-tcp`.

## 1 Forudsætninger

Installer og sæt PATH:

- CMake
- MSVC værktøjssæt eller anden understøttet C++ compiler
- ninja (valgfrit)
- SQLite3 development (SQLite3 CMake target findes ofte via vcpkg eller installer SDK)
- nlohmann_json (pakke-manager eller CMake `find_package`)

Præcise pakke-navne afhænger af din maskine-univers.

## 2 Konfigurer

Fra repo-rod Optihus (tilpas `-B`-mappen):

```powershell
cmake -S . -B build-windows-tcp
```

Kun klient på Windows til test mod Pi over netværk:

```powershell
cmake -S . -B build-windows-tcp -G Ninja -DOPTIHUS_BUILD_DATAHAANDTERING=OFF -DOPTIHUS_BUILD_MILJOESTYRING=ON
```

Begge mål normalt på Windows hvor SQLite findes til datahåndtering:

```powershell
cmake -S . -B build-windows-data -DOPTIHUS_BUILD_DATAHAANDTERING=ON -DOPTIHUS_BUILD_MILJOESTYRING=ON
```

Fejl om manglende `SQLite3`: du mangler stadig CMake/lib til SQLite eller `find_package`-sti.

## 3 Byg

```powershell
cmake --build build-windows-tcp --config Release
```

eller Ninja uden `--config`:

```powershell
cmake --build build-windows-tcp -j 8
```

## 4 Kør

Stil `miljoestyring_config.json` i **din nuværende arbejdsmappe** før du starter programmet:

```powershell
cd C:\sti\til\RepoRod
copy miljoestyring_config.json.example miljoestyring_config.json
.\build-windows-tcp\miljoestyring\miljoestyring.exe
```

Ret `server_host` i JSON til Pi 1 (fx `192.168.4.1` eller Pi ens LAN-ip).

### Web UI lokalt på Windows (hurtig UI-test)

Du kan køre **datahåndtering** på laptop og åbne web UI i browseren uden Pi 1 — godt til layout, formulare og API, ikke til reel GPIO.

**1. Byg kun serveren:**

```powershell
cd C:\sti\til\Optihus
cmake -S . -B build-windows -DOPTIHUS_BUILD_DATAHAANDTERING=ON -DOPTIHUS_BUILD_MILJOESTYRING=OFF
cmake --build build-windows --config Release
```

Kræver SQLite3 + nlohmann_json (fx via vcpkg). Se afsnit 1 ovenfor.

**2. Start fra repo-roden** (så `sql/init.sql` findes):

```powershell
cd C:\sti\til\Optihus
.\build-windows\datahaandtering\Release\datahaandtering.exe
```

Database oprettes som `data\optihus.db` under repo (Windows-standard). Valgfrit:

```powershell
$env:OPTIHUS_DB_PATH = "C:\sti\til\min.db"
.\build-windows\datahaandtering\Release\datahaandtering.exe --db data\optihus.db
```

**3. Browser:** [http://127.0.0.1:8080/](http://127.0.0.1:8080/)

Pi 2 behøver ikke være tilkoblet; status viser “frakoblet”. Når du er færdig med UI, deploy den nye binary til Pi som før.

### Hurtig genbuild + genstart (ét script)

Fra repo-roden i PowerShell:

```powershell
.\scripts\dev-datahaandtering.ps1
```

Ved UI-arbejde i `web_ui_server.cpp` — automatisk rebuild når du gemmer filen:

```powershell
.\scripts\dev-datahaandtering.ps1 -Watch
```

Derefter **Ctrl+F5** i browseren efter hver rebuild.

### Datahåndtering mod Pi fra Windows

Kør **miljoestyring** på Windows med `server_host` sat til Pi 1 — se afsnit 4.

### Web ui og firewall

Når programmet kører lokalt, binder det typisk til alle interfaces på port `8080` (`0.0.0.0:8080`). Browser på **samme pc**: `http://127.0.0.1:8080/` virker mere pålideligt med Windows Firewall aktiver end en anden enhed på LAN uden inbound-regler for port 8080.

## 5 Gentag Build efter `git pull`

```powershell
cd C:\sti\til\Optihus
git pull
cmake --build build-windows-tcp --config Release
```

Ved store CMake-ændringer: slet build-mappen eller brug ny `-B`-sti før du konfigurerer igen.

## Se også

- `vejledning/boot-og-netvaerk-da.md` (Linux systemd på Pi)
