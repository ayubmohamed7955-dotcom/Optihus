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
- [ ] `server_host` sat til `192.168.4.1`
- [ ] TCP exchange gennemført
- [ ] Browser UI tilgængelig på `http://192.168.4.1:8080`
- [ ] Hvis UI ikke åbner automatisk: manuel åbning af `http://192.168.4.1:8080` virker
- [ ] Fallback/admin TUI kan startes på Pi 1 med `./build/datahaandtering/datahaandtering --tui`

Resultat: **Pass / Fail**

## Beviser til demo/rapport

Gem mindst 3 artifacts:

1. Serverlog med `Listening on port 5000` og `Client connected: ...`
2. Klientlog med `Connected and sent hello ...`
3. `ss -tnp` / tilsvarende med `ESTABLISHED`
4. Browser-adgang til UI på `:8080`
