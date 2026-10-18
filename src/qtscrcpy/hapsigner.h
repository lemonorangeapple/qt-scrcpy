#pragma once

#include "commandresult.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QString>
#include <QStringList>

namespace QtScrcpy {

// Signs a HarmonyOS application package for the device that is connected right now, using the
// OpenHarmony signing materials that ship with the SDK (a debug certificate, a profile template and
// the hap-sign-tool). The device is asked for its UDID, the provisioning profile is generated for
// that UDID and signed, then the HAP is signed and can be installed.
//
// This is the same thing DevEco Studio's "automatic signing" does for OpenHarmony devices. A device
// that only trusts Huawei-issued profiles (a retail HarmonyOS phone) rejects this kind of profile;
// the error the device reports is passed on unchanged.
class QTSCRCPY_EXPORT HapSigner
{
public:
    // Where the signing materials are: `toolchains/` of the SDK, or the `signing/` directory next to
    // the application. Empty when they cannot be found. `QTSCRCPY_SIGNING_DIR` overrides the search.
    static QString findSigningDirectory();
    // A Java runtime: $JAVA_HOME, $PATH, then the JBR of a DevEco Studio installation.
    static QString findJava();

    // `signingDirectory`/`java` empty = findSigningDirectory()/findJava().
    HapSigner(const QString &signingDirectory = {}, const QString &java = {});
    ~HapSigner();
    HapSigner(const HapSigner &) = delete;
    HapSigner &operator=(const HapSigner &) = delete;

    void setSigningDirectory(const QString &directory) { m_directory = directory; }
    void setJava(const QString &java) { m_java = java; }
    // Bundle name written into the profile. Must match the HAP.
    void setBundleName(const QString &bundleName) { m_bundleName = bundleName; }
    // Minimum API version passed to the signing tool (the HAP's minAPIVersion).
    void setCompatibleVersion(int version) { m_compatibleVersion = version; }

    QString signingDirectory() const { return m_directory; }
    QString java() const { return m_java; }

    // Empty when everything was found, otherwise what is missing (for a readable error).
    QString checkReady() const;

    // The provisioning profile for `udid`: the SDK's debug template with this device, this bundle
    // name and a fresh validity. `error` is set when the template cannot be read.
    QString profileJson(const QString &udid, QString *error) const;

    // The application certificate chain file (leaf, CA, root) used for signing; it is shipped with the
    // materials, or rebuilt from the template plus the two authority certificates. Empty when unavailable.
    QString appCertChain(QString *error = nullptr) const;

    // Command lines used (exposed for tests and for logging).
    QStringList profileSignArguments(const QString &profileJsonPath, const QString &outP7b) const;
    QStringList appSignArguments(const QString &unsignedHap, const QString &profileP7b, const QString &outHap) const;

    // Signs `profileJsonPath` into `outP7b`. Returns false and sets `error` on failure.
    bool signProfile(const QString &profileJsonPath, const QString &outP7b, QString *error);
    // Signs `unsignedHap` with `profileP7b` into `outHap`.
    bool signApp(const QString &unsignedHap, const QString &profileP7b, const QString &outHap, QString *error);
    // signProfile() + signApp() in one step, with the profile written to a temporary file.
    bool signForDevice(const QString &udid, const QString &unsignedHap, const QString &outHap, QString *error);

private:
    bool runTool(const QStringList &arguments, QString *error) const;
    QString path(const QString &name) const;

    QString m_directory;
    QString m_java;
    QString m_bundleName = QStringLiteral("com.qtscrcpy.streamer");
    int m_compatibleVersion = 12;

    struct Impl;
    Impl *d = nullptr;
};

} // namespace QtScrcpy
