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
# directory, with a launcher link in bin: their generic names (adb, scrcpy) must not end up in a shared
# bin directory, where they would shadow or replace the user's own. Qt resolves the link, so the
# program still finds the package next to it. Windows and macOS installs are one private directory anyway.
if(UNIX AND NOT APPLE)
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
    # Find libqtscrcpy from wherever the executable is installed.
    file(RELATIVE_PATH _gui_to_lib "/${_gui_dir}" "/${CMAKE_INSTALL_LIBDIR}")
    if(APPLE)
        set_target_properties(qtscrcpy-gui PROPERTIES INSTALL_RPATH "@executable_path/${_gui_to_lib}")
    elseif(UNIX)
        set_target_properties(qtscrcpy-gui PROPERTIES INSTALL_RPATH "\$ORIGIN/${_gui_to_lib}")
    endif()
endif()

if(UNIX AND NOT APPLE)
    # bin/qtscrcpy-gui -> ../libexec/qt-scrcpy/qtscrcpy-gui (relative, so a moved prefix keeps working)
    file(RELATIVE_PATH _gui_link_target "/${CMAKE_INSTALL_BINDIR}" "/${_gui_dir}/qtscrcpy-gui")
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        file(CREATE_LINK \"${_gui_link_target}\"
             \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/qtscrcpy-gui\" SYMBOLIC)
        message(STATUS \"Linking: \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/qtscrcpy-gui -> ${_gui_link_target}\")
    ")
endif()
