add_library(optihus_shared
    src/message_codec.cpp
    src/logger.cpp
    src/time_utils.cpp
)

target_include_directories(optihus_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(nlohmann_json REQUIRED)
target_link_libraries(optihus_shared PUBLIC nlohmann_json::nlohmann_json)
