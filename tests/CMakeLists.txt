add_executable(tst_parsing tst_parsing.cpp)
target_link_libraries(tst_parsing PRIVATE qtscrcpy Qt6::Test)
add_test(NAME parsing COMMAND tst_parsing)

# The process tests drive shell-script stand-ins for adb and scrcpy.
if(UNIX)
    foreach(name tst_adb tst_scrcpy tst_monitor tst_compat tst_locate)
        add_executable(${name} ${name}.cpp)
        target_link_libraries(${name} PRIVATE qtscrcpy Qt6::Test)
        target_compile_definitions(${name} PRIVATE
            FAKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fake")
        add_test(NAME ${name} COMMAND ${name})
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
    target_link_libraries(tst_bundled PRIVATE qtscrcpy Qt6::Test)
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

    # The bundled scrcpy itself must accept the arguments this library generates.
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME compat_bundled COMMAND tst_compat)
        set_tests_properties(compat_bundled PROPERTIES
            ENVIRONMENT "SCRCPY_EXECUTABLE=${QTSCRCPY_BUNDLE_ROOT}/scrcpy;TST_COMPAT_REQUIRE_EXECUTABLE=${QTSCRCPY_BUNDLE_ROOT}/scrcpy")
    endif()
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
