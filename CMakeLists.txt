# wet - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(wet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(WET_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(wet WIN32 ${WET_SOURCES})
else()
    add_executable(wet ${WET_SOURCES})
endif()

rexglue_setup_target(wet)
