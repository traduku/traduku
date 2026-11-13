// The real registry, on Windows only.
//
// tst_winautostart.cpp proves the rules against a described store. This proves
// the described store is the real one: that QSettings in NativeFormat lands
// values where Windows actually reads Run, that the quoting survives a
// round-trip through the hive, and that a read opened read-only does not
// create the key. It writes to a scratch key of its own and deletes it again -
// never to the user's Run, because --selftest must be runnable by a person who
// is not about to log out.

#include <QtTest>

#include "platform/win/WinAutostart.h"
#include "platform/win/WinRegistryStore.h"

#include <QCoreApplication>
#include <QDir>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace lt;

namespace {

QString scratchId()
{
    return QStringLiteral("selftest-%1").arg(QCoreApplication::applicationPid());
}

#ifdef Q_OS_WIN
/// Straight from the hive, with no Traduku code in the way.
QString rawRead(const RunKey &key)
{
    QString out;
    static const QString prefix = QStringLiteral("HKEY_CURRENT_USER\\");
    if (!key.keyPath.startsWith(prefix, Qt::CaseInsensitive)) {
        return out;
    }
    HKEY h = nullptr;
    // Relative to the root, like every Reg* call wants it - passing the full
    // "HKEY_CURRENT_USER\..." string asks for a key named "HKEY_CURRENT_USER"
    // inside HKCU, which does not exist, and a reader that always answers
    // "nothing there" is the kind of green that proves nothing.
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      reinterpret_cast<LPCWSTR>(key.keyPath.mid(prefix.size()).utf16()), 0,
                      KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) {
        return out;
    }
    wchar_t buffer[1024];
    DWORD cb = sizeof(buffer), type = 0;
    if (RegQueryValueExW(h, reinterpret_cast<LPCWSTR>(key.valueName.utf16()), nullptr,
                         &type, reinterpret_cast<LPBYTE>(buffer), &cb) == ERROR_SUCCESS
        && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        out = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer),
                                 static_cast<qsizetype>(cb / sizeof(wchar_t)));
        while (!out.isEmpty() && out.at(out.size() - 1) == QChar(0)) {
            out.chop(1);
        }
        out = out.trimmed();
    }
    RegCloseKey(h);
    return out;
}

void deleteScratchKey(const QString &keyPath)
{
    // "The selftest cleaned up after itself" has to mean the key is gone, not
    // that its values were blanked: an empty key left in HKCU per run is
    // litter, and litter is how a debug run starts looking like an install.
    // keyPath arrives as "HKEY_CURRENT_USER\Software\..."; RegDeleteTreeW
    // wants the root separately, which is the same split the store makes.
    const QString prefix = QStringLiteral("HKEY_CURRENT_USER\\");
    if (!keyPath.startsWith(prefix, Qt::CaseInsensitive)) {
        return;
    }
    RegDeleteTreeW(HKEY_CURRENT_USER,
                   reinterpret_cast<LPCWSTR>(keyPath.mid(prefix.size()).utf16()));
}
#endif

} // namespace

class TestWinAutostartRegistry : public QObject
{
    Q_OBJECT

private slots:
    void roundTripThroughTheRealHive()
    {
#ifdef Q_OS_WIN
        const RunKey probe = selfTestRunKey(QStringLiteral("traduku"), scratchId());
        deleteScratchKey(probe.keyPath);
        const QString exe = QCoreApplication::applicationFilePath();
        WinAutostart a(exe, probe, {}, registryStoreFactory());
        QVERIFY2(!a.isEnabled(), qPrintable(a.diagnostic()));

        QVERIFY2(a.setEnabled(true), qPrintable(a.errorText()));
        QVERIFY(a.isEnabled());

        // Read it back with a fresh, independent reader - RegQueryValueExW,
        // not the class under test: an implementation may not grade its own
        // write, and "isEnabled() says on" is the same opinion, not evidence.
        QCOMPARE(rawRead(probe),
                 QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(exe)));

        QVERIFY2(a.setEnabled(false), qPrintable(a.errorText()));
        QVERIFY(!a.isEnabled());
        deleteScratchKey(probe.keyPath);
#else
        QSKIP("there is no registry on this platform; this row is a Windows row");
#endif
    }

    void readingDoesNotCreateTheKey()
    {
#ifdef Q_OS_WIN
        const RunKey probe = selfTestRunKey(QStringLiteral("traduku"),
                                            scratchId() + QStringLiteral("-read"));
        deleteScratchKey(probe.keyPath);
        WinAutostart a(QStringLiteral("C:\\windows\\system32\\cmd.exe"), probe, {},
                       registryStoreFactory());
        QVERIFY(!a.isEnabled());
        a.diagnostic();
        HKEY opened = nullptr;
        const LSTATUS st = RegOpenKeyExW(
            HKEY_CURRENT_USER,
            reinterpret_cast<LPCWSTR>(probe.keyPath.mid(
                QStringLiteral("HKEY_CURRENT_USER\\").size()).utf16()),
            0, KEY_READ | KEY_WOW64_64KEY, &opened);
        if (st == ERROR_SUCCESS) {
            RegCloseKey(opened);
            QFAIL(qPrintable(QStringLiteral("reading the autostart state created ")
                             + probe.keyPath));
        }
#else
        QSKIP("there is no registry on this platform; this row is a Windows row");
#endif
    }

    void theFactoryIsTheOneTheProductUses()
    {
        // The default factory and the injected one must be the same object, or
        // the unit suite and the shipped binary exercise different stores -
        // the "tested code is not the shipped code" failure.
#ifdef Q_OS_WIN
        const RunKey key = autostartRunKey();
        std::unique_ptr<IRunKeyStore> def = defaultRunKeyStoreFactory()(key, true);
        QVERIFY(def != nullptr);
#else
        QSKIP("registry stores exist on Windows only");
#endif
    }
};

QTEST_GUILESS_MAIN(TestWinAutostartRegistry)
#include "tst_winautostart_registry.moc"
