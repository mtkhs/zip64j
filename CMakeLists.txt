cmake_minimum_required(VERSION 3.21)

project(zip64j_project LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT, matching upstream zip 3.0 (windll/visualc/dll/zip32z64.dsp uses
# /MT). Keeps the DLLs free of a VC++ redistributable dependency, and stops
# UCRT dllimport declarations from turning windll.c's perror override into an
# implicit export.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "zip64j requires a 64-bit (x64) build. Pass -A x64.")
endif()

# Suppress a class of warnings uniformly across 3rd-party Info-ZIP code.
if(MSVC)
    add_compile_options(/W3 /utf-8)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

set(ZIP30_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zip30")
set(UNZIP60_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/unzip60")

include(cmake/zip30.cmake)     # provides ZIP30_SOURCES / ZIP30_INCLUDES / ZIP30_DEFINES
include(cmake/zip64j.cmake)
include(cmake/unzip60.cmake)   # provides UNZIP60_SOURCES / UNZIP60_INCLUDES / UNZIP60_DEFINES
include(cmake/unzip64.cmake)

# Dist directory aggregates final DLLs for downstream consumption.
# Both DLLs are self-contained (zip30 / unzip60 statically linked).
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")
add_custom_target(dist ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:zip64j>"  "${DIST_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:unzip64>" "${DIST_DIR}/"
    DEPENDS zip64j unzip64
    COMMENT "Aggregating DLLs into ${DIST_DIR}"
)

# Smoke tests live in tests/CMakeLists.txt. Disable with -DZIP64J_BUILD_TESTS=OFF
# when consuming zip64j as a subproject or building release-only artifacts.
option(ZIP64J_BUILD_TESTS "Build smoke tests" ON)
if(ZIP64J_BUILD_TESTS)
    add_subdirectory(tests)
endif()
