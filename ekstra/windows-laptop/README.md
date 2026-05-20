# windows-laptop — udvikling uden Raspberry Pi

Alt her er **valgfrit** til laptop/Windows. **Pi-drift** bruger kun rod-mapperne `datahaandtering/`, `miljoestyring/`, `shared/`, `sql/`.

| Indhold | Formål |
|---------|--------|
| `scripts/` | PowerShell/shell: dev-server, smoke-test (stub) |
| `vejledning-windows/` | CMake-build af datahaandtering på Windows |

Pi 2 (`miljoestyring`) bygges **kun på Linux** (GPIO).

**`archived-win32-sources/`** — kopi af kildefiler med `#if _WIN32` før de blev Linux-only i rod-træet.
