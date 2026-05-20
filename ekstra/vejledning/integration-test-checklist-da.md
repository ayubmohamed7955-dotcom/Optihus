# Optihus - Integrationstest checklist (TCP)

Brug denne liste til at dokumentere at kommunikation virker stabilt.

## Test A - Server først

- [ ] Start server
- [ ] Bekræft `Listening on port 5000`
- [ ] Start klient
- [ ] Bekræft klient `Connected and sent hello ...`
- [ ] Bekræft server `Client connected: <ip>`
- [ ] Bekræft socket `ESTABLISHED` mellem to forskellige IP'er

Resultat: **Pass / Fail**

## Test B - Klient først (recovery)

- [ ] Start klient før server
- [ ] Start server bagefter
- [ ] Bekræft at forbindelse opstår uden manuel procesrestart
- [ ] Bekræft samme logs som i Test A

Resultat: **Pass / Fail**

## Test C - AP mode

- [ ] Klient er på AP-netværk
- [ ] Klient har `192.168.4.x` adresse
- [ ] Klient peger mod Pi 1 som AP‑gateway (**`192.168.4.1`**, felterne **`kDefaultDataServerHost`** / **`kTcpPort`**)
- [ ] TCP exchange gennemført
- [ ] Web ui tilgængelig på `http://192.168.4.1:8080`
- [ ] Hvis UI ikke åbner automatisk: manuel åbning af `http://192.168.4.1:8080` virker
- [ ] Fallback/admin TUI kan startes på Pi 1 med `./build/datahaandtering/datahaandtering --tui`

Resultat: **Pass / Fail**

## Test D - AP tabt og gendannet (recovery)

- [ ] Start server + klient og bekræft normal forbindelse
- [ ] Sluk server/AP midlertidigt
- [ ] Bekræft klient fortsætter reconnect-forsøg (ingen manuel restart)
- [ ] Start server/AP igen
- [ ] Bekræft klient reconnecter automatisk
- [ ] Bekræft server logger ny forbindelse fra klient-IP

Resultat: **Pass / Fail**

## Test E — Persistens og felt-overlay (valgfri)

- [ ] Efter **`set_config`** findes **`miljoestyring_field_config.json`** (standard **`/var/lib/optihus/`** eller **`OPTIHUS_STATE_DIR`**) og kan læses af felt-brugeren
- [ ] Genstart af **`miljoestyring`** anvender replay (**log**: genindsat persistert feltkonfiguration) før TCP er oppe
- [ ] I Web-ui → **Fejlsøgning: teknisk JSON** viser **`field_node`** med **`components`** når Pi 2 sender telemetri med **`field_live`**

Resultat: **Pass / Fail**

## Beviser til demo/rapport

Gem mindst 3 artifacts:

1. Serverlog med `Listening on port 5000` og `Client connected: ...`
2. Klientlog med `Connected and sent hello ...`
3. `ss -tnp` / tilsvarende med `ESTABLISHED`
4. Browser-adgang til UI på `:8080`
