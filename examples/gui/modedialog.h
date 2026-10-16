#pragma once

#include <qtscrcpy/scrcpyoptions.h>

#include <QDialog>

class QComboBox;

// Shown when a device becomes usable: pick screen mirroring or camera mirroring.
class ModeDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Choice { None, Screen, Camera };

    // `cameraUnavailableReason` is empty when camera mirroring can be used; otherwise the
    // camera button is disabled and the reason is shown.
    ModeDialog(const QString &deviceName, const QString &serial, const QString &cameraUnavailableReason,
               QWidget *parent = nullptr);

    Choice choice() const { return m_choice; }
    QtScrcpy::CameraFacing cameraFacing() const;

private:
    Choice m_choice = Choice::None;
    QComboBox *m_facing = nullptr;
};
