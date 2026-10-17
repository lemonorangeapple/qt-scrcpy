cmake_minimum_required(VERSION 3.16)
project(qt-scrcpy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Examples and tests are only built by default when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_qtscrcpy_top_level ON)
else()
    set(_qtscrcpy_top_level OFF)
endif()
option(QTSCRCPY_BUILD_EXAMPLES "Build the Qt Widgets demo application" ${_qtscrcpy_top_level})
option(QTSCRCPY_BUILD_TESTS "Build the unit tests" ${_qtscrcpy_top_level})
# AUTO: bundle scrcpy and adb where the scrcpy project publishes a package, otherwise warn and go on;
# ON: bundle or fail; OFF: use what is installed. Needs network access the first time (the package is
# cached afterwards); see cmake/QtScrcpyBundle.cmake. Off by default when this is a subproject.
if(_qtscrcpy_top_level)
    set(_qtscrcpy_bundle_default AUTO)
else()
    set(_qtscrcpy_bundle_default OFF)
endif()
# A parent project may have set QTSCRCPY_BUNDLE_SCRCPY as a normal variable before add_subdirectory(). Unless
# policy CMP0126 (CMake 3.21) is NEW, set(... CACHE ...) deletes a normal variable of the same name, so put it back.
if(DEFINED QTSCRCPY_BUNDLE_SCRCPY)
    set(_qtscrcpy_bundle_requested "${QTSCRCPY_BUNDLE_SCRCPY}")
endif()
set(QTSCRCPY_BUNDLE_SCRCPY "${_qtscrcpy_bundle_default}" CACHE STRING
    "Bundle scrcpy and adb next to the demo application: AUTO (where scrcpy publishes a package), ON (required) or OFF")
if(DEFINED _qtscrcpy_bundle_requested)
    set(QTSCRCPY_BUNDLE_SCRCPY "${_qtscrcpy_bundle_requested}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QtScrcpyBundle)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

add_subdirectory(src)

if(QTSCRCPY_BUILD_EXAMPLES)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    add_subdirectory(examples/gui)
endif()

if(QTSCRCPY_BUILD_TESTS)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
