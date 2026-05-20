# Arkiveret: miljøstyring uden GPIO (stub-telemetri)

Tidligere kunne `miljoestyring` bygges med `OPTIHUS_MILJOESTYRING_HARDWARE=OFF` (fx på Windows):
faste måleværdier, ingen motor/pumpe, manuelle tests ignoreret.

**Feltnoden forudsætter nu Linux på Raspberry Pi med GPIO/I2C** (`libgpiod`, hw-drivere).

Se `CMakeLists.stub.cmake` for den gamle CMake-logik. Stub-grene i `app.cpp` / `sensor_manager.cpp`
er fjernet fra aktiv kildekode.
