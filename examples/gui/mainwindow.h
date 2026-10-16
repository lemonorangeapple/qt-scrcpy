#pragma once

#include <qtscrcpy/adb.h>
#include <qtscrcpy/scrcpy.h>

#include <QHash>
#include <QMainWindow>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

// Demo front-end for the qtscrcpy library: lists adb devices, mirrors the selected
// one with scrcpy and offers a few adb actions.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void rebuildDeviceList(const QList<QtScrcpy::AdbDevice> &devices);
    void updateButtons();
    void log(const QString &message);
    QString selectedSerial() const;
    QtScrcpy::ScrcpyOptions currentOptions() const;
    QtScrcpy::Scrcpy *sessionFor(const QString &serial);

    void connectWireless();
    void switchToWifi();
    void startMirroring();
    void stopMirroring();
    void takeScreenshot();
    void installApk();
    void runShellCommand();
    void logResult(const QString &what, const QtScrcpy::CommandResult &result);

    QtScrcpy::Adb m_adb;
    QHash<QString, QtScrcpy::Scrcpy *> m_sessions; // one scrcpy per device serial

    QListWidget *m_deviceList = nullptr;
    QLineEdit *m_address = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_wifiButton = nullptr;

    QSpinBox *m_maxSize = nullptr;
    QSpinBox *m_bitRate = nullptr;
    QSpinBox *m_maxFps = nullptr;
    QCheckBox *m_showTouches = nullptr;
    QCheckBox *m_stayAwake = nullptr;
    QCheckBox *m_turnScreenOff = nullptr;
    QCheckBox *m_alwaysOnTop = nullptr;
    QCheckBox *m_noControl = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;

    QPushButton *m_screenshotButton = nullptr;
    QPushButton *m_installButton = nullptr;
    QPushButton *m_homeButton = nullptr;
    QPushButton *m_backButton = nullptr;
    QLineEdit *m_shellCommand = nullptr;
    QPushButton *m_shellButton = nullptr;

    QPlainTextEdit *m_log = nullptr;
};
