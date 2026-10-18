cmake_minimum_required(VERSION 3.5.0)
project(streamer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenH264 (BSD-2-Clause) is built from source as the software H.264 encoder for devices without a
# hardware one. Only the portable C++ code is compiled (no assembly), so every ABI builds the same way.
set(OPENH264_ROOT "$ENV{QTSCRCPY_OPENH264_DIR}" CACHE PATH "OpenH264 source tree (v2.6.0)")
if(NOT OPENH264_ROOT)
    set(OPENH264_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openh264")
endif()

set(QSC_SOFTWARE_ENCODER OFF)
if(EXISTS "${OPENH264_ROOT}/codec/api/wels/codec_api.h")
    set(QSC_SOFTWARE_ENCODER ON)
    set(_H264_SRC "")
    foreach(_dir common encoder processing)
        file(GLOB_RECURSE _found "${OPENH264_ROOT}/codec/${_dir}/*.cpp")
        list(FILTER _found EXCLUDE REGEX "/(arm|arm64|x86|mips|loongarch|riscv)/|/DllEntry\.cpp$|/console/")
        list(APPEND _H264_SRC ${_found})
    endforeach()
    add_library(openh264_sw STATIC ${_H264_SRC})
    target_include_directories(openh264_sw PUBLIC
        "${OPENH264_ROOT}/codec/api/wels"
        "${OPENH264_ROOT}/codec/api")
    target_include_directories(openh264_sw PRIVATE
        "${OPENH264_ROOT}/codec/common/inc"
        "${OPENH264_ROOT}/codec/encoder/core/inc"
        "${OPENH264_ROOT}/codec/encoder/plus/inc"
        "${OPENH264_ROOT}/codec/processing/interface"
        "${OPENH264_ROOT}/codec/processing/src/common"
        "${OPENH264_ROOT}/codec/processing/src/adaptivequantization"
        "${OPENH264_ROOT}/codec/processing/src/backgrounddetection"
        "${OPENH264_ROOT}/codec/processing/src/complexityanalysis"
        "${OPENH264_ROOT}/codec/processing/src/denoise"
        "${OPENH264_ROOT}/codec/processing/src/downsample"
        "${OPENH264_ROOT}/codec/processing/src/imagerotate"
        "${OPENH264_ROOT}/codec/processing/src/scenechangedetection"
        "${OPENH264_ROOT}/codec/processing/src/scrolldetection"
        "${OPENH264_ROOT}/codec/processing/src/vaacalc")
    target_compile_definitions(openh264_sw PRIVATE ENCODER_CORE)
    set_target_properties(openh264_sw PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(streamer SHARED napi_init.cpp streamer.cpp softencoder.cpp)
if(QSC_SOFTWARE_ENCODER)
    target_compile_definitions(streamer PRIVATE QSC_HAVE_OPENH264)
    target_link_libraries(streamer PRIVATE openh264_sw)
endif()
target_include_directories(streamer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(streamer PUBLIC
    libace_napi.z.so
    libhilog_ndk.z.so
    libnative_window.so
    libnative_buffer.so
    libnative_media_codecbase.so
    libnative_media_core.so
    libnative_media_venc.so
    libnative_avscreen_capture.so
    libohcamera.so)
