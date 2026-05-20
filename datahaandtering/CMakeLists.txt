add_executable(datahaandtering
    src/main.cpp
    src/app.cpp
    src/tcp_server.cpp
    src/database_manager.cpp
    src/config_manager.cpp
    src/command_dispatcher.cpp
    src/telemetry_processor.cpp
    src/web_ui_server.cpp
)

target_include_directories(datahaandtering
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(datahaandtering PRIVATE _DEFAULT_SOURCE)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

if(TARGET SQLite3::SQLite3)
    set(OPTIHUS_SQLITE3_TARGET SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(OPTIHUS_SQLITE3_TARGET SQLite::SQLite3)
else()
    message(FATAL_ERROR "SQLite3 imported target not found (expected SQLite3::SQLite3 or SQLite::SQLite3)")
endif()

target_link_libraries(datahaandtering
    PRIVATE
        optihus_shared
        ${OPTIHUS_SQLITE3_TARGET}
        Threads::Threads
)
