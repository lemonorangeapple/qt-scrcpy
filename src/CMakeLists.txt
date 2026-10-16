include(GenerateExportHeader)

add_library(qtscrcpy
    qtscrcpy/adb.cpp
    qtscrcpy/adb.h
    qtscrcpy/adbdevice.cpp
    qtscrcpy/adbdevice.h
    qtscrcpy/commandresult.h
    qtscrcpy/commandrunner.cpp
    qtscrcpy/commandrunner.h
    qtscrcpy/locate.cpp
    qtscrcpy/locate.h
    qtscrcpy/scrcpy.cpp
    qtscrcpy/scrcpy.h
    qtscrcpy/scrcpyoptions.cpp
    qtscrcpy/scrcpyoptions.h
)
add_library(QtScrcpy::qtscrcpy ALIAS qtscrcpy)

generate_export_header(qtscrcpy
    BASE_NAME qtscrcpy
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/qtscrcpy/qtscrcpy_export.h)

target_include_directories(qtscrcpy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
target_link_libraries(qtscrcpy PUBLIC Qt6::Core)
target_compile_features(qtscrcpy PUBLIC cxx_std_17)
set_target_properties(qtscrcpy PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
    target_compile_options(qtscrcpy PRIVATE /W4)
else()
    target_compile_options(qtscrcpy PRIVATE -Wall -Wextra)
endif()
