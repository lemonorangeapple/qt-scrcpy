// Where Adb looks for its program: $ADB, Android SDK
// directories, $PATH, and how a value with a directory part is read.
#include "testenvironment.h"

#include <qtscrcpy/adb.h>
#include <qtscrcpy/locate.h>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace QtScrcpy;

class TstLocate : public QObject
{
    Q_OBJECT
private slots:
    void init();

    void resolveExecutable_bareNamesAreSearchedInPath();
    void resolveExecutable_valuesWithADirectoryAreRelativeToTheCurrentDirectory();
    void adb_environmentVariableWins();
    void adb_unresolvableEnvironmentVariableIsIgnored();
    void adb_androidSdkDirectories();
    void adb_defaultSdkDirectoryInHome();
    void adb_pathIsTheLastResort();

private:
    // A directory holding the given executable files.
    QString dirWith(const QStringList &names)
    {
        auto *dir = new QTemporaryDir;
        m_dirs.push_back(std::unique_ptr<QTemporaryDir>(dir));
        for (const QString &name : names) {
            QDir().mkpath(QFileInfo(dir->filePath(name)).absolutePath());
            if (!makeExecutableFile(dir->filePath(name)))
                return {};
        }
        return dir->path();
    }

    std::vector<std::unique_ptr<QTemporaryDir>> m_dirs;
};

void TstLocate::init()
{
    m_dirs.clear();
    // The files below must be executable: skip everything on a noexec TMPDIR.
    const QString probe = dirWith({"probe"});
    if (probe.isEmpty())
        QSKIP("TMPDIR does not allow executable files (noexec mount?)");
}

void TstLocate::resolveExecutable_bareNamesAreSearchedInPath()
{
    const QString pathDir = dirWith({"tool"});
    IsolatedProgramEnvironment isolated(pathDir, pathDir);
    QCOMPARE(QFileInfo(resolveExecutable("tool")).canonicalFilePath(), QFileInfo(pathDir + "/tool").canonicalFilePath());
    QVERIFY(resolveExecutable("no-such-tool").isEmpty());
    QVERIFY(resolveExecutable({}).isEmpty());
}

void TstLocate::resolveExecutable_valuesWithADirectoryAreRelativeToTheCurrentDirectory()
{
    // $PATH has a directory that also contains "adb" and "sub/adb"; neither is what "./adb" or
    // "sub/adb" means. The names are looked up as paths, not in every $PATH entry.
    const QString pathDir = dirWith({"adb", "sub/adb"});
    const QString cwdDir = dirWith({"adb", "sub/adb"});
    IsolatedProgramEnvironment isolated(pathDir, pathDir);
    const QString oldCwd = QDir::currentPath();
    QVERIFY(QDir::setCurrent(cwdDir));
    const auto restore = qScopeGuard([&] { QDir::setCurrent(oldCwd); });

    QCOMPARE(QFileInfo(resolveExecutable("./adb")).canonicalFilePath(), QFileInfo(cwdDir + "/adb").canonicalFilePath());
    QCOMPARE(QFileInfo(resolveExecutable("sub/adb")).canonicalFilePath(), QFileInfo(cwdDir + "/sub/adb").canonicalFilePath());
    QCOMPARE(QFileInfo(resolveExecutable(cwdDir + "/adb")).canonicalFilePath(), QFileInfo(cwdDir + "/adb").canonicalFilePath());
    QVERIFY(resolveExecutable("./nothing-here").isEmpty());
    QVERIFY(resolveExecutable("nodir/adb").isEmpty()); // not under the current directory, although $PATH has "adb"
}

void TstLocate::adb_environmentVariableWins()
{
    const QString chosen = dirWith({"my-adb"});
    const QString pathDir = dirWith({"adb"});
    IsolatedProgramEnvironment isolated(pathDir, pathDir);
    ScopedEnvironmentVariable adb("ADB", (chosen + "/my-adb").toUtf8());
    QCOMPARE(QFileInfo(Adb::findExecutable()).canonicalFilePath(), QFileInfo(chosen + "/my-adb").canonicalFilePath());
}

void TstLocate::adb_unresolvableEnvironmentVariableIsIgnored()
{
    const QString pathDir = dirWith({"adb"});
    IsolatedProgramEnvironment isolated(pathDir, pathDir);
    ScopedEnvironmentVariable adb("ADB", "/nonexistent/adb");
    QCOMPARE(QFileInfo(Adb::findExecutable()).canonicalFilePath(), QFileInfo(pathDir + "/adb").canonicalFilePath());
}

void TstLocate::adb_androidSdkDirectories()
{
    const QString pathDir = dirWith({"adb"});
    const QString sdk = dirWith({"platform-tools/adb"});
    const QString emptyHome = dirWith({});
    for (const char *variable : {"ANDROID_SDK_ROOT", "ANDROID_HOME"}) {
        IsolatedProgramEnvironment isolated(pathDir, emptyHome);
        ScopedEnvironmentVariable root(variable, sdk.toUtf8());
        // The SDK directory comes before $PATH.
        QCOMPARE(QFileInfo(Adb::findExecutable()).canonicalFilePath(),
                 QFileInfo(sdk + "/platform-tools/adb").canonicalFilePath());
    }
}

void TstLocate::adb_defaultSdkDirectoryInHome()
{
    const QString pathDir = dirWith({"adb"});
    const QString home = dirWith({"Android/Sdk/platform-tools/adb"});
    IsolatedProgramEnvironment isolated(pathDir, home);
    QCOMPARE(QFileInfo(Adb::findExecutable()).canonicalFilePath(),
             QFileInfo(home + "/Android/Sdk/platform-tools/adb").canonicalFilePath());
}

void TstLocate::adb_pathIsTheLastResort()
{
    const QString pathDir = dirWith({"adb"});
    const QString emptyHome = dirWith({});
    {
        IsolatedProgramEnvironment isolated(pathDir, emptyHome);
        QCOMPARE(QFileInfo(Adb::findExecutable()).canonicalFilePath(), QFileInfo(pathDir + "/adb").canonicalFilePath());
    }
    IsolatedProgramEnvironment isolated(emptyHome, emptyHome);
    QVERIFY2(Adb::findExecutable().isEmpty(), qPrintable(Adb::findExecutable()));
}

QTEST_GUILESS_MAIN(TstLocate)
#include "tst_locate.moc"
