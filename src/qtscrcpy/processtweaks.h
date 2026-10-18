#pragma once

#include <QProcess>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace QtScrcpy {
namespace detail {

// A GUI program has no console on Windows, so a console child (adb, scrcpy's own
// window is separate) would open one of its own. CREATE_NO_WINDOW suppresses it.
inline void suppressConsoleWindow(QProcess &process)
{
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#else
    Q_UNUSED(process)
#endif
}

} // namespace detail
} // namespace QtScrcpy
