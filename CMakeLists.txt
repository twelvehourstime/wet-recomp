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
    src/crash/crash_handler.cpp
    src/patches/framerate.cpp
    src/patches/mouselook.cpp
    src/input_defaults.cpp
    src/debug/memtool.cpp
    src/input/pad_remap.cpp
    src/ui/controls_menu.cpp
    src/ui/display_mode.cpp
)

if(WIN32)
    add_executable(wet WIN32 ${WET_SOURCES})
else()
    add_executable(wet ${WET_SOURCES})
endif()

rexglue_setup_target(wet GPU_PLUGINS xenos)

if(WIN32)
    target_link_libraries(wet PRIVATE dbghelp)  # crash reporter + memtool watch
endif()

# Crash reporter self-test: links only the crash handler + SDK runtime, never loads the game.
add_executable(crash_selftest src/crash/crash_selftest.cpp src/crash/crash_handler.cpp)
target_include_directories(crash_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(crash_selftest PRIVATE rex::runtime dbghelp)
rexglue_apply_target_settings(crash_selftest)
add_custom_command(TARGET crash_selftest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:crash_selftest> $<TARGET_FILE_DIR:crash_selftest>
    COMMAND_EXPAND_LISTS)

# cvar save/load self-test (links the SDK runtime only; never loads the game).
add_executable(cvar_selftest src/debug/cvar_selftest.cpp)
target_link_libraries(cvar_selftest PRIVATE rex::runtime)
rexglue_apply_target_settings(cvar_selftest)
