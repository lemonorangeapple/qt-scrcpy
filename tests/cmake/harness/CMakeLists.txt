# Qt-free project that exercises cmake/QtScrcpyBundle.cmake; driven by test_bundle_module.cmake.
cmake_minimum_required(VERSION 3.16)
project(bundle_module_harness CXX)

list(APPEND CMAKE_MODULE_PATH "${MODULE_DIR}")

if(USE_SUBDIRECTORY)
    # The module is included in a subdirectory (like qt-scrcpy used as a subproject) and
    # the function is called from here, a different directory scope.
    add_subdirectory(sub)
else()
    include(QtScrcpyBundle)
endif()

file(WRITE "${CMAKE_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
add_executable(app "${CMAKE_BINARY_DIR}/main.cpp")
qtscrcpy_bundle_prepare(bundle_ready)
message(STATUS "HARNESS bundle_ready=${bundle_ready}")
qtscrcpy_bundle_runtime(app)
install(TARGETS app RUNTIME DESTINATION bin)

if(TARGET_COUNT)
    # More bundling targets that all write to the same (default) output directory.
    foreach(i RANGE 1 ${TARGET_COUNT})
        add_executable(app${i} "${CMAKE_BINARY_DIR}/main.cpp")
        qtscrcpy_bundle_runtime(app${i} NO_INSTALL)
    endforeach()
endif()
