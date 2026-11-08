// Windows autostart: HKCU\...\CurrentVersion\Run.
//
// The class is written against a store interface for one reason - the
// assertions that matter cannot be made against the real registry from a test
// without logging the user out: "reading the state did not write it" and "a
// value somebody else owns is not ours to delete". tst_xdgautostart caught the
// first disease on Linux by accident (reading wrote the file on every start),
// so here it is an assertion, not a hope. The real-registry path is covered
// separately by tst_winautostart_registry, on Windows only.

#include <QtTest>

#include "platform/win/WinAutostart.h"

#include <QHash>
#include <QQueue>

using namespace lt;

namespace {

class FakeStore : public IRunKeyStore
{
public:
    /// Backed by a table that outlives the store: a fresh open of the same key
    /// must see what an earlier open wrote, or the fake forgets on the class's
    /// behalf and every "it was written" assertion below passes for the wrong
    /// reason. (It did, once: `disablingRemovesOnlyOurOwnValue` was green while
    /// the store dropped every write.)
    explicit FakeStore(QHash<QString, QString> *values, bool readOnly)
        : m_values(values)
        , m_openedReadOnly(readOnly)
    {
    }

    bool contains(const QString &value) const override
    {
        ++reads;
        if (!m_openedReadOnly) {
            opensWritableForRead = true;      // the disease we are testing for
        }
        return m_values->contains(value);
    }
    QString value(const QString &value) const override
    {
        ++reads;
        if (!m_openedReadOnly) {
            opensWritableForRead = true;
        }
        return m_values->value(value);
    }
    void setValue(const QString &value, const QString &text) override
    {
        writes.append(value + QLatin1Char('=') + text);
        m_values->insert(value, text);
    }
    void remove(const QString &value) override
    {
        removals.append(value);
        m_values->remove(value);
    }

    mutable int reads = 0;
    mutable bool opensWritableForRead = false;
    QStringList writes;
    QStringList removals;

private:
    QHash<QString, QString> *m_values;
    bool m_openedReadOnly = true;
};

/// Hands out stores the way the class will hand out registry handles, and
/// records how every one of them was opened.
class FakeFactory
{
public:
    RunKeyStoreFactory factory()
    {
        return [this](const RunKey &key, bool readOnly) -> std::unique_ptr<IRunKeyStore> {
            opened << QStringLiteral("%1:%2").arg(key.keyPath,
                                                  readOnly ? QStringLiteral("ro")
                                                           : QStringLiteral("rw"));
            return std::make_unique<FakeStore>(&tables[key.keyPath], readOnly);
        };
    }
    QHash<QString, QHash<QString, QString>> tables;   // keyPath -> values
    QStringList opened;

    QHash<QString, QString> &table(const RunKey &key) { return tables[key.keyPath]; }
    /// Substring, not element: the recorded strings are "<keyPath>:rw", and
    /// QStringList::contains() asks for an equal element - which is how this
    /// helper was green about nothing.
    bool writableOpen() const
    {
        for (const QString &o : opened) {
            if (o.endsWith(QLatin1String(":rw"))) {
                return true;
            }
        }
        return false;
    }
    bool anyOpen() const { return !opened.isEmpty(); }
};

const char *const kExe = "C:\\Program Files\\Traduku\\traduku.exe";

} // namespace

class TestWinAutostart : public QObject
{
    Q_OBJECT

private slots:
    void offByDefaultAndSaysSo()
    {
        FakeFactory f;
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {}, f.factory());
        QVERIFY(!a.isEnabled());
        QVERIFY2(a.diagnostic().contains(QStringLiteral("off")),
                 qPrintable(a.diagnostic()));
        // The answer has to name where it looked: "off" alone cannot be
        // distinguished from "this build does not do autostart".
        QVERIFY2(a.diagnostic().contains(QStringLiteral("Run")),
                 qPrintable(a.diagnostic()));
    }

    void enablingWritesOurExecutableIntoTheRunKey()
    {
        FakeFactory f;
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {}, f.factory());
        QVERIFY(a.setEnabled(true));
        const QString written = f.table(autostartRunKey()).value(QStringLiteral("traduku"));
        // Quoted, because "C:\Program Files\..." without quotes is two
        // arguments to the registry's Run parser and never launches.
        QCOMPARE(written, QStringLiteral("\"C:\\Program Files\\Traduku\\traduku.exe\""));
        QVERIFY(a.isEnabled());
        QVERIFY(f.writableOpen());
    }

    void disablingRemovesOnlyOurOwnValue()
    {
        FakeFactory f;
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {}, f.factory());
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.setEnabled(false));
        QVERIFY(!a.isEnabled());
        QCOMPARE(f.table(autostartRunKey()).size(), 0);
    }

    void readingTheStateNeverWritesIt()
    {
        FakeFactory f;
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {}, f.factory());
        a.isEnabled();
        a.diagnostic();
        a.backendName();
        QVERIFY2(!f.writableOpen(),
                 qPrintable(QStringLiteral("a read opened the key for writing: ")
                            + f.opened.join(QStringLiteral(", "))));
        QVERIFY(f.anyOpen());      // not green because nothing was asked
    }

    void aForeignValueIsNotOursToDelete()
    {
        FakeFactory f;
        f.table(autostartRunKey()).insert(QStringLiteral("traduku"),
                                          QStringLiteral("C:\\other\\thing.exe"));
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {}, f.factory());
        // It DOES start at login - pretending otherwise would let the app
        // report "autostart off" while it keeps coming back.
        QVERIFY(a.isEnabled());
        QVERIFY2(a.diagnostic().contains(QStringLiteral("not this executable")),
                 qPrintable(a.diagnostic()));
        QVERIFY(!a.setEnabled(false));
        QVERIFY(a.isEnabled());
        QVERIFY2(a.errorText().contains(QStringLiteral("will not delete")),
                 qPrintable(a.errorText()));
        QCOMPARE(f.table(autostartRunKey()).size(), 1);   // untouched
    }

    void aMachineWideEntryIsReportedAndNotTouched()
    {
        // Parity with XdgAutostart's /etc/xdg/autostart lesson: an entry the
        // app does not own still starts the app, and a menu that ticks itself
        // off while the app keeps autostarting is the bug that lesson was.
        FakeFactory f;
        const RunKey machine = machineRunKey();
        f.table(machine).insert(QStringLiteral("traduku"),
                                QStringLiteral("\"C:\\Program Files\\Traduku\\traduku.exe\""));
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {machine}, f.factory());
        QVERIFY(a.isEnabled());
        QVERIFY2(a.diagnostic().contains(QStringLiteral("machine-wide")),
                 qPrintable(a.diagnostic()));
        QVERIFY(!a.setEnabled(false));
        QVERIFY(a.isEnabled());                    // still starts: and it says so
        QVERIFY2(a.diagnostic().contains(QStringLiteral("administrator")),
                 qPrintable(a.diagnostic()));
    }

    void theUserEntryTurnsTheAppOnAndOffOnItsOwn()
    {
        FakeFactory f;
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {machineRunKey()},
                       f.factory());
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.isEnabled());
        QVERIFY(a.setEnabled(false));              // no machine entry in this table
        QVERIFY(!a.isEnabled());
    }

    void backendNameNamesTheHiveAndTheKey()
    {
        FakeFactory f;
        WinAutostart a(QString::fromUtf8(kExe), autostartRunKey(), {}, f.factory());
        QVERIFY2(a.backendName().contains(QStringLiteral("registry")),
                 qPrintable(a.backendName()));
        QVERIFY2(a.backendName().contains(QStringLiteral("Run")),
                 qPrintable(a.backendName()));
    }

    void selfTestKeyIsNotTheRealOne()
    {
        // --selftest runs the same class the product runs, on the real code
        // path, and must still never touch the user's login sequence.
        const RunKey real = autostartRunKey();
        const RunKey probe = selfTestRunKey(QStringLiteral("traduku"),
                                            QStringLiteral("selftest-42"));
        QVERIFY2(probe.keyPath != real.keyPath,
                 "the selftest would write the user's autostart");
        QVERIFY2(!probe.keyPath.contains(QStringLiteral("CurrentVersion")),
                 qPrintable(probe.keyPath));
        QVERIFY2(probe.keyPath.contains(QStringLiteral("selftest-42")),
                 qPrintable(probe.keyPath));
        QCOMPARE(probe.valueName, real.valueName);
    }

    void locationDefaultsMatchWhatWindowsReads()
    {
        QCOMPARE(autostartRunKey().keyPath.toUpper(),
                 QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\MICROSOFT\\WINDOWS"
                                "\\CURRENTVERSION\\RUN"));
        QCOMPARE(autostartRunKey().valueName, QStringLiteral("traduku"));
        QVERIFY(machineRunKey().keyPath.startsWith(QStringLiteral("HKEY_LOCAL_MACHINE")));
    }
};

QTEST_GUILESS_MAIN(TestWinAutostart)
#include "tst_winautostart.moc"
