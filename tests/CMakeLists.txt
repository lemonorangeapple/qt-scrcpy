add_executable(tst_parsing tst_parsing.cpp)
target_link_libraries(tst_parsing PRIVATE qtscrcpy Qt5::Test)
add_test(NAME parsing COMMAND tst_parsing)
qtscrcpy_copy_dlls(tst_parsing)

# A small stand-in for hdc, driven by environment variables, used by the fallback tests.
add_executable(fakehdc fake/hdc.c)

add_executable(tst_hdcfallback tst_hdcfallback.cpp)
target_link_libraries(tst_hdcfallback PRIVATE qtscrcpy Qt5::Test Qt5::Network)
target_compile_definitions(tst_hdcfallback PRIVATE
    SAMPLE_JPEG="${CMAKE_CURRENT_SOURCE_DIR}/data/sample.jpg"
    FAKE_HDC_PATH="$<TARGET_FILE:fakehdc>")
add_test(NAME hdcfallback COMMAND tst_hdcfallback)
qtscrcpy_copy_dlls(tst_hdcfallback)

add_executable(tst_signing tst_signing.cpp)
target_link_libraries(tst_signing PRIVATE qtscrcpy Qt5::Test)
target_compile_definitions(tst_signing PRIVATE SIGNING_DIR="${PROJECT_SOURCE_DIR}/signing"
        CMAKE_COMMAND="${CMAKE_COMMAND}")
add_test(NAME signing COMMAND tst_signing)
qtscrcpy_copy_dlls(tst_signing)

add_executable(tst_stream tst_stream.cpp)
target_link_libraries(tst_stream PRIVATE qtscrcpy Qt5::Test)
target_compile_definitions(tst_stream PRIVATE SAMPLE_H264="${CMAKE_CURRENT_SOURCE_DIR}/data/sample.h264")
add_test(NAME stream COMMAND tst_stream)
qtscrcpy_copy_dlls(tst_stream)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_executable(tst_scrcpystream tst_scrcpystream.cpp)
    target_link_libraries(tst_scrcpystream PRIVATE qtscrcpy Qt5::Test Qt5::Network)
    target_compile_definitions(tst_scrcpystream PRIVATE
        SAMPLE_H264="${CMAKE_CURRENT_SOURCE_DIR}/data/sample.h264"
        FAKE_SERVER_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_adb_server.py")
    add_test(NAME scrcpystream COMMAND tst_scrcpystream)
    qtscrcpy_copy_dlls(tst_scrcpystream)

    # The plain C interface, driven by a C program (no C++, no Qt headers).
    add_executable(tst_cinterface tst_cinterface.c)
    target_include_directories(tst_cinterface PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(tst_cinterface PRIVATE qtscrcpy)
    set_target_properties(tst_cinterface PROPERTIES LINKER_LANGUAGE CXX)
    qtscrcpy_copy_dlls(tst_cinterface)
    add_test(NAME cinterface COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_cinterface.py
             $<TARGET_FILE:tst_cinterface> ${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_adb_server.py
             ${CMAKE_CURRENT_SOURCE_DIR}/data/sample.h264)
endif()

# The process tests drive shell-script stand-ins for adb.
if(UNIX)
    foreach(name tst_adb tst_monitor tst_locate)
        add_executable(${name} ${name}.cpp)
        target_link_libraries(${name} PRIVATE qtscrcpy Qt5::Test)
        target_compile_definitions(${name} PRIVATE
            FAKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fake")
        add_test(NAME ${name} COMMAND ${name})
        qtscrcpy_copy_dlls(${name})
    endforeach()
endif()

# The scrcpy/adb package that qtscrcpy_bundle_runtime() ships next to executables.
# (Only together with the demo application: a tests-only build never needs the download.)
set(_qtscrcpy_bundle_ready FALSE)
if(QTSCRCPY_BUILD_EXAMPLES)
    qtscrcpy_bundle_prepare(_qtscrcpy_bundle_ready)
endif()
if(_qtscrcpy_bundle_ready)
    add_executable(tst_bundled tst_bundled.cpp)
    target_link_libraries(tst_bundled PRIVATE qtscrcpy Qt5::Test)
    target_compile_definitions(tst_bundled PRIVATE EXPECTED_SCRCPY_VERSION="${QTSCRCPY_BUNDLE_VERSION}")
    if(QTSCRCPY_BUNDLE_HAS_ADB)
        target_compile_definitions(tst_bundled PRIVATE BUNDLED_ADB=1)
    else()
        target_compile_definitions(tst_bundled PRIVATE BUNDLED_ADB=0)
    endif()
    # Its own directory, so the package is not copied next to every other test.
    set_target_properties(tst_bundled PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bundled")
    qtscrcpy_bundle_runtime(tst_bundled NO_INSTALL)
    add_test(NAME tst_bundled COMMAND tst_bundled)
    qtscrcpy_copy_dlls(tst_bundled)
endif()

# The module that downloads and unpacks the scrcpy package, tested without network and without
# the real packages (stand-in archives, including hostile ones when python is available).
if(UNIX)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_test(NAME bundle_module
        COMMAND ${CMAKE_COMMAND}
            -DMODULE_DIR=${PROJECT_SOURCE_DIR}/cmake
            -DHARNESS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/harness
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/bundle_module_work
            -DQT_SCRCPY_DIR=${PROJECT_SOURCE_DIR} -DWITH_QT=ON
            -DPYTHON=${Python3_EXECUTABLE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_bundle_module.cmake)
endif()
