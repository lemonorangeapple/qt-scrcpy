add_executable(qtscrcpy-gui
    main.cpp
    mainwindow.cpp
    mainwindow.h
    modedialog.cpp
    modedialog.h
)
target_link_libraries(qtscrcpy-gui PRIVATE qtscrcpy Qt6::Widgets)
