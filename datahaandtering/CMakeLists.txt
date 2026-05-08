add_executable(datahaandtering
    src/main.cpp
    src/app.cpp
    src/tcp_server.cpp
    src/database_manager.cpp
    src/config_manager.cpp
    src/command_dispatcher.cpp
    src/telemetry_processor.cpp
    src/tui_manager.cpp
    src/web_ui_server.cpp
)

target_include_directories(datahaandtering
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(datahaandtering
    PRIVATE
        optihus_shared
        SQLite::SQLite3
        Threads::Threads
)

if(WIN32)
    target_link_libraries(datahaandtering PRIVATE ws2_32)
endif()
