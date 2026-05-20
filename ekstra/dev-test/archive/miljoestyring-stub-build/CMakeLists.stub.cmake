# Tidligere miljoestyring/CMakeLists.txt (uddrag) — OPTIHUS_MILJOESTYRING_HARDWARE

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(OPTIHUS_MILJOESTYRING_HARDWARE "GPIO/I2C driver build (Raspberry Pi)" ON)
else()
    set(OPTIHUS_MILJOESTYRING_HARDWARE OFF CACHE BOOL "GPIO/I2C driver build (Raspberry Pi)" FORCE)
endif()

if(OPTIHUS_MILJOESTYRING_HARDWARE)
    target_sources(miljoestyring PRIVATE
        "src/hw/Ventilation system/DHT22.cpp"
        "src/hw/Ventilation system/Motor.cpp"
        "src/hw/Watering system/PlantBox.cpp"
    )
    target_compile_definitions(miljoestyring PRIVATE OPTIHUS_MILJOESTYRING_HARDWARE=1)
    target_link_libraries(miljoestyring PRIVATE gpiodcxx gpiod)
else()
    target_compile_definitions(miljoestyring PRIVATE OPTIHUS_MILJOESTYRING_HARDWARE=0)
endif()
