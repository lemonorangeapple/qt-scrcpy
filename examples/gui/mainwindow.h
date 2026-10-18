#pragma once

#include <qtscrcpy/adb.h>
#include <qtscrcpy/devicemonitor.h>
#include <qtscrcpy/hdc.h>
#include <qtscrcpy/mirror.h>

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSet>

class QComboBox;
class QListWidget;
class QPushButton;
class VideoWindow;

// Minimal front-end: one list with adb and hdc devices, a screen/camera choice and one start/stop button.
// Every device, whichever tool reaches it, is mirrored through QtScrcpy::MirrorSession.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    struct Entry {
        QtScrcpy::AdbDevice device;
        QtScrcpy::Backend backend = QtScrcpy::Backend::Adb;
    };

    void buildUi();
    void rebuildDeviceList();
    void updateUi();
    void toggleMirroring();
    void refreshDevices();
    // One line about the tools and what was found, shown in the status bar.
    QString toolingSummary() const;
    void startMirroring(const Entry &entry);
    void reportError(const QString &message);

    Entry selectedEntry() const;
    bool isMirroring(const QString &key) const;
    static QString keyOf(const Entry &entry);
    QtScrcpy::MirrorSource currentSource() const;

    QtScrcpy::Adb m_adb;
    QtScrcpy::DeviceMonitor m_monitor{&m_adb};
    QtScrcpy::Hdc m_hdc;
    QHash<QString, QtScrcpy::MirrorSession *> m_sessions;   // by backend + serial
    QHash<QString, QPointer<VideoWindow>> m_windows;
    QSet<QString> m_reported;
    QString m_lastAdbProblem; // the last thing adb complained about // messages already shown in a dialog

    QListWidget *m_deviceList = nullptr;
    QComboBox *m_source = nullptr;
    QPushButton *m_toggleButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
};
