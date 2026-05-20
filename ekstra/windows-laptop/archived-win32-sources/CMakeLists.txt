cmake_minimum_required(VERSION 3.16)

project(optihus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPTIHUS_BUILD_DATAHAANDTERING "Build datahaandtering target" ON)
option(OPTIHUS_BUILD_MILJOESTYRING "Build miljoestyring target" ON)

if(WIN32)
    set(OPTIHUS_BUILD_MILJOESTYRING OFF CACHE BOOL "miljoestyring is Linux/Pi only" FORCE)
endif()

add_subdirectory(shared)

if(OPTIHUS_BUILD_DATAHAANDTERING)
    add_subdirectory(datahaandtering)
endif()

if(OPTIHUS_BUILD_MILJOESTYRING)
    add_subdirectory(miljoestyring)
endif()
