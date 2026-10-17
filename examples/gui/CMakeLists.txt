add_executable(qtscrcpy-gui
    main.cpp
    mainwindow.cpp
    mainwindow.h
    modedialog.cpp
    modedialog.h
)
target_link_libraries(qtscrcpy-gui PRIVATE qtscrcpy Qt6::Widgets)

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
