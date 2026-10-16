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
