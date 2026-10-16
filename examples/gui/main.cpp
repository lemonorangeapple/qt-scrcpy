#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qtscrcpy-gui"));

    MainWindow window;
    window.show();
    return app.exec();
}
