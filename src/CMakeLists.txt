include(GenerateExportHeader)

add_library(qtscrcpy
    qtscrcpy/adb.cpp
    qtscrcpy/adb.h
    qtscrcpy/adbclient.cpp
    qtscrcpy/adbclient.h
    qtscrcpy/adbdevice.cpp
    qtscrcpy/adbdevice.h
    qtscrcpy/commandresult.h
    qtscrcpy/commandrunner.cpp
    qtscrcpy/commandrunner.h
    qtscrcpy/devicemonitor.cpp
    qtscrcpy/devicemonitor.h
    qtscrcpy/h264decoder.cpp
    qtscrcpy/h264decoder.h
    qtscrcpy/hapsigner.cpp
    qtscrcpy/hapsigner.h
    qtscrcpy/imagedecoder.cpp
    qtscrcpy/imagedecoder.h
    qtscrcpy/hdc.cpp
    qtscrcpy/hdc.h
    qtscrcpy/hdcstream.cpp
    qtscrcpy/hdcstream.h
    qtscrcpy/locate.cpp
    qtscrcpy/locate.h
    qtscrcpy/mirror.cpp
    qtscrcpy/mirror.h
    qtscrcpy/processtweaks.h
    qtscrcpy/qtscrcpy_c.cpp
    qtscrcpy/qtscrcpy_c.h
    qtscrcpy/scrcpystream.cpp
    qtscrcpy/scrcpystream.h
    qtscrcpy/streamprotocol.cpp
    qtscrcpy/streamprotocol.h
)
add_library(QtScrcpy::qtscrcpy ALIAS qtscrcpy)

generate_export_header(qtscrcpy
    BASE_NAME qtscrcpy
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/qtscrcpy/qtscrcpy_export.h)

target_include_directories(qtscrcpy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
target_link_libraries(qtscrcpy PUBLIC Qt5::Core Qt5::Network)

# FFmpeg decodes the H.264 stream of the HarmonyOS streamer. Point QTSCRCPY_FFMPEG_ROOT at a directory
# with include/ and lib/ (and bin/ with the DLLs on Windows); without it, streaming is compiled out.
set(QTSCRCPY_FFMPEG_ROOT "" CACHE PATH "FFmpeg development package (include/, lib/, bin/) for HarmonyOS streaming")
set(QTSCRCPY_FFMPEG_DLLS "")
if(QTSCRCPY_FFMPEG_ROOT)
    set(_ff_libs "")
    foreach(_name avcodec avutil swscale)
        find_library(_ff_${_name} NAMES ${_name} lib${_name} PATHS "${QTSCRCPY_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        if(NOT _ff_${_name})
            message(FATAL_ERROR "QTSCRCPY_FFMPEG_ROOT=${QTSCRCPY_FFMPEG_ROOT}: lib${_name} not found in lib/")
        endif()
        list(APPEND _ff_libs "${_ff_${_name}}")
    endforeach()
    if(NOT EXISTS "${QTSCRCPY_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR "QTSCRCPY_FFMPEG_ROOT=${QTSCRCPY_FFMPEG_ROOT}: include/libavcodec/avcodec.h not found")
    endif()
    target_include_directories(qtscrcpy PRIVATE "${QTSCRCPY_FFMPEG_ROOT}/include")
    target_link_libraries(qtscrcpy PRIVATE ${_ff_libs})
    target_compile_definitions(qtscrcpy PRIVATE QTSCRCPY_HAVE_FFMPEG)
    if(WIN32)
        # Only what the decoder links against (avcodec needs avutil and swresample at run time).
        file(GLOB QTSCRCPY_FFMPEG_DLLS "${QTSCRCPY_FFMPEG_ROOT}/bin/avcodec-*.dll" "${QTSCRCPY_FFMPEG_ROOT}/bin/avutil-*.dll"
             "${QTSCRCPY_FFMPEG_ROOT}/bin/swscale-*.dll" "${QTSCRCPY_FFMPEG_ROOT}/bin/swresample-*.dll")
    endif()
    set(QTSCRCPY_FFMPEG_DLLS "${QTSCRCPY_FFMPEG_DLLS}" CACHE INTERNAL "FFmpeg DLLs to ship next to executables")
    message(STATUS "qt-scrcpy: H.264 streaming enabled (FFmpeg at ${QTSCRCPY_FFMPEG_ROOT})")
else()
    message(STATUS "qt-scrcpy: H.264 streaming disabled (no QTSCRCPY_FFMPEG_ROOT)")
endif()
target_compile_features(qtscrcpy PUBLIC cxx_std_17)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(qtscrcpy PUBLIC QTSCRCPY_C_STATIC)
endif()
set_target_properties(qtscrcpy PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PREFIX "") # qtscrcpy.dll, not libqtscrcpy.dll

# hdc (HarmonyOS Device Connector) and what it needs to run, shipped next to the executables so that
# Hdc::findExecutable() finds it there. A directory with hdc.exe (and its DLLs) in it.
set(QTSCRCPY_HDC_DIR "${CMAKE_SOURCE_DIR}/build/deps/hdc" CACHE PATH "Directory with hdc and its libraries to ship next to the executables")
set(QTSCRCPY_HDC_FILES "")
if(IS_DIRECTORY "${QTSCRCPY_HDC_DIR}")
    file(GLOB QTSCRCPY_HDC_FILES "${QTSCRCPY_HDC_DIR}/hdc" "${QTSCRCPY_HDC_DIR}/hdc.exe" "${QTSCRCPY_HDC_DIR}/*.dll"
         "${QTSCRCPY_HDC_DIR}/*.so*" "${QTSCRCPY_HDC_DIR}/*.dylib")
    message(STATUS "qt-scrcpy: shipping hdc from ${QTSCRCPY_HDC_DIR}")
else()
    message(STATUS "qt-scrcpy: no hdc to ship (${QTSCRCPY_HDC_DIR} does not exist); hdc is looked up on this machine")
endif()
set(QTSCRCPY_HDC_FILES "${QTSCRCPY_HDC_FILES}" CACHE INTERNAL "hdc files to ship next to executables")

# The companion application (see harmony/build_hap.cmd), shipped the same way: HdcStream looks for it next to
# the program and installs it on a phone that lacks it.
set(QTSCRCPY_HAP "${CMAKE_SOURCE_DIR}/qtscrcpy-harmony-unsigned.hap" CACHE FILEPATH "Companion application package to ship next to the executables")
set(QTSCRCPY_HAP_FILES "")
if(EXISTS "${QTSCRCPY_HAP}")
    set(QTSCRCPY_HAP_FILES "${QTSCRCPY_HAP}")
    message(STATUS "qt-scrcpy: shipping the companion application ${QTSCRCPY_HAP}")
endif()
set(QTSCRCPY_HAP_FILES "${QTSCRCPY_HAP_FILES}" CACHE INTERNAL "companion application package to ship")

# The OpenHarmony signing materials (hap-sign-tool.jar, debug certificate, profile template), shipped in a
# signing/ directory next to the executables where HapSigner::findSigningDirectory() looks for them.
set(QTSCRCPY_SIGNING_SOURCE "${CMAKE_SOURCE_DIR}/signing" CACHE PATH "Directory with the HAP signing materials to ship as signing/ next to the executables")
if(EXISTS "${QTSCRCPY_SIGNING_SOURCE}/hap-sign-tool.jar")
    message(STATUS "qt-scrcpy: shipping the signing materials from ${QTSCRCPY_SIGNING_SOURCE}")
else()
    message(STATUS "qt-scrcpy: no signing materials to ship (${QTSCRCPY_SIGNING_SOURCE}/hap-sign-tool.jar is missing)")
    set(QTSCRCPY_SIGNING_SOURCE "" CACHE PATH "" FORCE)
endif()

# Windows has no rpath: an executable only finds qtscrcpy.dll (and the FFmpeg DLLs) in its own directory.
# qtscrcpy_copy_dlls(<target>) copies them there after every link, together with hdc and the companion application.
function(qtscrcpy_copy_dlls target)
    set(_files ${QTSCRCPY_HDC_FILES} ${QTSCRCPY_HAP_FILES})
    if(WIN32)
        list(APPEND _files ${QTSCRCPY_FFMPEG_DLLS})
        if(BUILD_SHARED_LIBS)
            list(APPEND _files "$<TARGET_FILE:qtscrcpy>")
        endif()
    endif()
    if(_files)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_files} "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying runtime files next to ${target}" VERBATIM)
    endif()
    if(QTSCRCPY_SIGNING_SOURCE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${QTSCRCPY_SIGNING_SOURCE}" "$<TARGET_FILE_DIR:${target}>/signing"
            COMMENT "Copying the signing materials next to ${target}" VERBATIM)
    endif()
endfunction()

if(MSVC)
    target_compile_options(qtscrcpy PRIVATE /W4)
else()
    target_compile_options(qtscrcpy PRIVATE -Wall -Wextra)
endif()
