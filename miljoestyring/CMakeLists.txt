if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "miljoestyring builds only on Linux (Raspberry Pi field node with GPIO/I2C)")
endif()

add_executable(miljoestyring
    src/main.cpp
    src/app.cpp
    src/tcp_client.cpp
    src/sensor_manager.cpp
    src/control_manager.cpp
    src/set_config_store.cpp
    "src/hw/ClimateControl/DHT22.cpp"
    "src/hw/ClimateControl/Motor.cpp"
    "src/hw/WateringSystem/PlantBox.cpp"
)

# Keep HW source untouched: compile this file with unsigned char semantics
# so byte literals like 0xC3/0x83 are valid on platforms with signed char.
set_source_files_properties(
    "src/hw/WateringSystem/PlantBox.cpp"
    PROPERTIES COMPILE_OPTIONS "-funsigned-char"
)

target_link_libraries(miljoestyring PRIVATE gpiodcxx gpiod)

target_include_directories(miljoestyring
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(miljoestyring PRIVATE _DEFAULT_SOURCE)

find_package(Threads REQUIRED)

target_link_libraries(miljoestyring
    PRIVATE
        optihus_shared
        Threads::Threads
)
