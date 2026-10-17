# A project that uses qt-scrcpy as a subproject, the way the README describes; driven by
# test_bundle_module.cmake (needs Qt6, like qt-scrcpy itself).
cmake_minimum_required(VERSION 3.16)
project(consumer CXX)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

set(QTSCRCPY_BUNDLE_SCRCPY ON) # the documented way to ask for bundling as a subproject
add_subdirectory("${QT_SCRCPY_DIR}" qt-scrcpy)

file(WRITE "${CMAKE_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
add_executable(myapp "${CMAKE_BINARY_DIR}/main.cpp")
target_link_libraries(myapp PRIVATE QtScrcpy::qtscrcpy)
qtscrcpy_bundle_prepare(consumer_bundle_ready)
message(STATUS "CONSUMER bundle_ready=${consumer_bundle_ready}")
qtscrcpy_bundle_runtime(myapp INSTALL_DESTINATION libexec/myapp)
install(TARGETS myapp RUNTIME DESTINATION libexec/myapp)
