add_executable(miljoestyring
    src/main.cpp
    src/app.cpp
    src/tcp_client.cpp
    src/sensor_manager.cpp
    src/actuator_manager.cpp
    src/control_manager.cpp
)

target_include_directories(miljoestyring
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)

target_link_libraries(miljoestyring
    PRIVATE
        optihus_shared
        Threads::Threads
)
