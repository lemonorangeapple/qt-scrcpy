add_executable(tst_parsing tst_parsing.cpp)
target_link_libraries(tst_parsing PRIVATE qtscrcpy Qt6::Test)
add_test(NAME parsing COMMAND tst_parsing)

# The process tests drive shell-script stand-ins for adb and scrcpy.
if(UNIX)
    foreach(name tst_adb tst_scrcpy tst_monitor tst_compat)
        add_executable(${name} ${name}.cpp)
        target_link_libraries(${name} PRIVATE qtscrcpy Qt6::Test)
        target_compile_definitions(${name} PRIVATE
            FAKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fake")
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
endif()
