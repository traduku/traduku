#include <QtTest>
#include <QSignalSpy>

#include "core/settings.h"
#include "core/shortcutmonitor.h"
#include "interfaces/iglobalshortcut.h"

using namespace lt;

namespace {
class FakeShortcut : public IGlobalShortcut
{
    Q_OBJECT
public:
    using IGlobalShortcut::IGlobalShortcut;
    QString registered;
    bool failRegister = false;
    bool registerShortcut(const QString &seq) override
    {
        registered = seq;
        return !failRegister;
    }
    void unregister() override { registered.clear(); }
    QString backendName() const override { return QStringLiteral("fake"); }
};
} // namespace

class TestShortcutMonitor : public QObject
{
    Q_OBJECT
    qint64 m_now = 1000;
    QTemporaryDir m_dir;

private slots:
    void init() { m_now = 1000; }

    void registersConfiguredSequence()
    {
        Settings s(m_dir.path() + QStringLiteral("/c.ini"));
        s.setGlobalShortcut(QStringLiteral("Ctrl+Alt+T"));
        FakeShortcut fake;
        ShortcutMonitor mon(&fake, &s);
        QVERIFY(mon.start());
        QCOMPARE(fake.registered, QString("Ctrl+Alt+T"));
    }

    void registerFailurePropagates()
    {
        Settings s(m_dir.path() + QStringLiteral("/c2.ini"));
        FakeShortcut fake;
        fake.failRegister = true;
        ShortcutMonitor mon(&fake, &s);
        QVERIFY(!mon.start());
    }

    void singleTriggerDoesNotActivate()
    {
        Settings s(m_dir.path() + QStringLiteral("/c3.ini"));
        s.setPressInterval(0.5);
        FakeShortcut fake;
        ShortcutMonitor mon(&fake, &s);
        mon.setClock([this]() { return m_now; });
        QSignalSpy activated(&mon, &ShortcutMonitor::activated);
        QVERIFY(mon.start());
        Q_EMIT fake.triggered();
        QCOMPARE(activated.count(), 0);
    }

    void doubleTriggerActivatesOnce()
    {
        Settings s(m_dir.path() + QStringLiteral("/c4.ini"));
        s.setPressInterval(0.5);
        FakeShortcut fake;
        ShortcutMonitor mon(&fake, &s);
        mon.setClock([this]() { return m_now; });
        QSignalSpy activated(&mon, &ShortcutMonitor::activated);
        QVERIFY(mon.start());
        Q_EMIT fake.triggered();
        m_now += 200;
        Q_EMIT fake.triggered();
        QCOMPARE(activated.count(), 1);
        m_now += 100;
        Q_EMIT fake.triggered();          // starts a new gesture
        QCOMPARE(activated.count(), 1);
        m_now += 9999;                     // expired
        Q_EMIT fake.triggered();
        QCOMPARE(activated.count(), 1);
    }

    void hotReloadsShortcut()
    {
        Settings s(m_dir.path() + QStringLiteral("/c5.ini"));
        FakeShortcut fake;
        ShortcutMonitor mon(&fake, &s);
        QVERIFY(mon.start());
        QCOMPARE(fake.registered, QString("Ctrl+Alt+C"));
        s.setGlobalShortcut(QStringLiteral("Ctrl+Alt+Q"));   // app saves
        QTRY_COMPARE_WITH_TIMEOUT(fake.registered, QString("Ctrl+Alt+Q"), 5000);
    }

};

QTEST_GUILESS_MAIN(TestShortcutMonitor)
#include "tst_shortcutmonitor.moc"
