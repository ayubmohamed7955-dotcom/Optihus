# Arkiveret: TUI (terminal-UI)

Tidligere fallback/admin via `datahaandtering --tui`.

**Drift bruger kun web-UI** (port 8080). Disse filer bygges ikke længere med projektet.

Gendannelse: kopier tilbage til `datahaandtering/src/tui_manager.cpp` og
`datahaandtering/include/datahaandtering/tui_manager.hpp`, tilføj `src/tui_manager.cpp` i
`datahaandtering/CMakeLists.txt`, og genindfør `--tui` i `datahaandtering/src/main.cpp`.
