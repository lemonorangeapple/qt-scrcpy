#pragma once

#include "commandresult.h"

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

namespace QtScrcpy {

// Internal helper that runs a short-lived command and collects its output,
// either blocking (runSync) or through the event loop (start + finished).
class CommandRunner : public QObject
{
    Q_OBJECT
public:
    static constexpr int kNoTimeout = -1;

    static CommandResult runSync(const QString &program,
                                 const QStringList &arguments,
                                 int timeoutMs,
                                 const QProcessEnvironment &environment = {});

    explicit CommandRunner(QObject *parent = nullptr);
    ~CommandRunner() override;

    // Starts the command; `finished` is emitted exactly once, after which the
    // runner deletes itself.
    void start(const QString &program,
               const QStringList &arguments,
               int timeoutMs,
               const QProcessEnvironment &environment = {});

signals:
    void finished(const QtScrcpy::CommandResult &result);

private:
    void finish(CommandError error, const QString &message = {});

    QProcess m_process;
    QTimer m_timer;
    CommandResult m_result;
    bool m_done = false;
};

} // namespace QtScrcpy
