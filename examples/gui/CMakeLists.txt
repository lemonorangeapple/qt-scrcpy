add_executable(qtscrcpy-gui
    main.cpp
    mainwindow.cpp
    mainwindow.h
    videowindow.cpp
    videowindow.h
)
target_link_libraries(qtscrcpy-gui PRIVATE qtscrcpy Qt5::Widgets)

# No console window when the GUI is started on Windows.
if(WIN32)
    set_target_properties(qtscrcpy-gui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(GNUInstallDirs)

# On Unix the executable and the scrcpy/adb package it brings are installed together into a private
# directory: their generic names (adb, scrcpy) must not end up in a shared bin directory (the default
# prefix /usr/local is also Homebrew's on macOS), where they would shadow or replace the user's own.
# Linux also gets a launcher link in bin; Qt resolves the link, so the program still finds the package
# next to it (not verified on macOS, so there the program is started from its private directory).
# A Windows install goes to <Program Files>/<project> by default, which is private already.
if(UNIX)
    set(_gui_dir "${CMAKE_INSTALL_LIBEXECDIR}/qt-scrcpy")
else()
    set(_gui_dir "${CMAKE_INSTALL_BINDIR}")
endif()

# scrcpy and adb next to the executable: the library looks there.
qtscrcpy_bundle_runtime(qtscrcpy-gui INSTALL_DESTINATION "${_gui_dir}")

install(TARGETS qtscrcpy-gui RUNTIME DESTINATION "${_gui_dir}")

if(BUILD_SHARED_LIBS)
    install(TARGETS qtscrcpy
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

    # Find libqtscrcpy from wherever the executable is installed: relative to the executable when both
    # directories are relative to the prefix; otherwise an absolute path (nothing relative to compute).
    if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
        set(_gui_rpath "${CMAKE_INSTALL_LIBDIR}")
    elseif(IS_ABSOLUTE "${_gui_dir}")
        set(_gui_rpath "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    else()
        file(RELATIVE_PATH _gui_to_lib "/${_gui_dir}" "/${CMAKE_INSTALL_LIBDIR}")
        if(APPLE)
            set(_gui_rpath "@executable_path/${_gui_to_lib}")
        else()
            set(_gui_rpath "\$ORIGIN/${_gui_to_lib}")
        endif()
    endif()
    if(APPLE OR UNIX)
        set_target_properties(qtscrcpy-gui PROPERTIES INSTALL_RPATH "${_gui_rpath}")
    endif()
endif()

if(UNIX AND NOT APPLE)
    configure_file(install-launcher.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/install-launcher.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-launcher.cmake")
endif()

# qtscrcpy.dll and the FFmpeg DLLs next to the executable (Windows), so it runs with nothing installed.
qtscrcpy_copy_dlls(qtscrcpy-gui)
if(WIN32)
    set(_runtime_dlls ${QTSCRCPY_FFMPEG_DLLS})
    if(BUILD_SHARED_LIBS)
        list(APPEND _runtime_dlls "$<TARGET_FILE:qtscrcpy>")
    endif()
    if(_runtime_dlls)
        install(FILES ${_runtime_dlls} DESTINATION "${_gui_dir}")
    endif()
endif()
if(QTSCRCPY_SIGNING_SOURCE)
    install(DIRECTORY "${QTSCRCPY_SIGNING_SOURCE}/" DESTINATION "${_gui_dir}/signing")
endif()
if(QTSCRCPY_HDC_FILES OR QTSCRCPY_HAP_FILES)
    install(PROGRAMS ${QTSCRCPY_HDC_FILES} DESTINATION "${_gui_dir}")
    install(FILES ${QTSCRCPY_HAP_FILES} DESTINATION "${_gui_dir}")
endif()

# Qt's own DLLs, the platform plugin and the compiler runtime next to the executable on Windows, so the
# program runs straight from the build directory and from the install directory. windeployqt knows which
# plugins and runtime libraries a Qt build needs; it is found next to Qt's own DLLs.
if(WIN32)
    get_target_property(_qt_core Qt5::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _qt_core)
        get_target_property(_qt_core Qt5::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(_qt_bin "${_qt_core}" DIRECTORY)
    find_program(QTSCRCPY_WINDEPLOYQT windeployqt HINTS "${_qt_bin}" NO_DEFAULT_PATH)
    if(NOT QTSCRCPY_WINDEPLOYQT)
        message(FATAL_ERROR "qt-scrcpy: windeployqt not found in ${_qt_bin}; it is needed to put Qt next to the executable.")
    endif()
    # windeployqt finds qmake and the compiler runtime through PATH: put Qt's bin (and the compiler's, for the
    # runtime DLLs) in front of it for this one command.
    get_filename_component(_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_deploy_path "${_qt_bin};${_cxx_bin}")
    set(_deploy_args --no-translations --no-system-d3d-compiler --no-opengl-sw --no-angle --no-svg)
    add_custom_command(TARGET qtscrcpy-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${_deploy_path};$ENV{PATH}"
                "${QTSCRCPY_WINDEPLOYQT}" ${_deploy_args} "$<TARGET_FILE:qtscrcpy-gui>"
        COMMENT "Running windeployqt"
        VERBATIM)
    set(_deploy_path_native "${_deploy_path}")
    file(TO_NATIVE_PATH "${QTSCRCPY_WINDEPLOYQT}" _windeployqt_native)
    install(CODE "
        set(_deploy_cmd \"${_windeployqt_native}\" ${_deploy_args}
            --dir \"\${CMAKE_INSTALL_PREFIX}/${_gui_dir}\" \"$<TARGET_FILE:qtscrcpy-gui>\")
        set(ENV{PATH} \"${_deploy_path};\$ENV{PATH}\")
        file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${_gui_dir}\")
        execute_process(COMMAND \${_deploy_cmd} RESULT_VARIABLE _deploy_result)
        if(NOT _deploy_result EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed (\${_deploy_result})\")
        endif()
    ")
endif()
