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
    "src/hw/Ventilation system/DHT22.cpp"
    "src/hw/Ventilation system/Motor.cpp"
    "src/hw/Watering system/PlantBox.cpp"
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
