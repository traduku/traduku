#include <QtTest>
#include <QDBusConnection>
#include <QSignalSpy>

#include "app/SingleInstance.h"
#include "app/AppFlavour.h"
#include "fakebus.h"

using namespace lt;

class TestSingleInstance : public QObject
{
    Q_OBJECT
    test::PrivateBus m_bus;

private slots:
    void initTestCase()
    {
        if (!m_bus.start()) {
            QSKIP("dbus-daemon not available");
        }
    }

    void advertisedBusName()
    {
        // D5: the GNOME single-instance token is dev.traduku.Traduku (the
        // .desktop advertises it); org.kde.* belongs to the KDE shell only.
        SingleInstance s;
        QCOMPARE(s.busName(), QStringLiteral("dev.traduku.Traduku"));
    }

    void firstInstanceTakesOwnership()
    {
        SingleInstance s;
        QVERIFY2(s.start(m_bus.bus()), qPrintable(s.errorText()));
        QVERIFY(s.errorText().isEmpty());
    }

    void secondInstanceIsRefusedAndWakesTheFirst()
    {
        SingleInstance first;
        QVERIFY(first.start(m_bus.bus()));

        QSignalSpy woke(&first, &SingleInstance::activateRequested);
        SingleInstance second;
        QDBusConnection otherClient = m_bus.connect(QStringLiteral("traduku-test-second-client"));
        QVERIFY(otherClient.isConnected());
        QVERIFY2(!second.start(otherClient),
                 "a second copy claimed ownership while one was running");
        QVERIFY2(second.errorText().contains(QStringLiteral("dev.traduku.Traduku")),
                 qPrintable(second.errorText()));
        // and it must have asked the running one to surface itself
        QTRY_COMPARE_WITH_TIMEOUT(woke.count(), 1, 5000);
    }

    void releasingOwnershipLetsAnotherInstanceStart()
    {
        {
            SingleInstance holding;
            QVERIFY(holding.start(m_bus.bus()));
        }                                        // destructor releases the name
        SingleInstance next;
        QVERIFY2(next.start(m_bus.bus()), qPrintable(next.errorText()));
    }

    void noBusIsReportedNotIgnored()
    {
        SingleInstance s;
        QDBusConnection dead = QDBusConnection::connectToBus(
            QStringLiteral("unix:path=/nonexistent-traduku-socket"),
            QStringLiteral("traduku-test-dead"));
        QVERIFY(!dead.isConnected());
        QVERIFY(!s.start(dead));
        QVERIFY2(!s.errorText().isEmpty(), "the operator gets no reason");
        // Measured, not styled: `env -i .../traduku` - how the harness and any
        // user over ssh invokes the binary - answered "Traduku is already
        // running (asked it to surface its window)" with nothing running, and
        // exited 0. Both shells printed the duplicate sentence for every
        // failure of start(), so a missing session bus was reported as a
        // second copy the user should go and look for.
        QVERIFY2(s.errorText().contains(QStringLiteral("no session bus")),
                 qPrintable(s.errorText()));
        QVERIFY2(!s.errorText().contains(QStringLiteral("already")),
                 qPrintable(QStringLiteral("a bus-less start is not a duplicate: ")
                            + s.errorText()));
    }

    void theShellMessageNamesTheReasonItRefusedToStart()
    {
        // The decision both shells print, in one shared function so neither
        // drifts back to one sentence for every failure.
        const QString owned =
            QStringLiteral("dev.traduku.Traduku is already owned by a running Traduku");
        const lt::SingleInstanceVerdict dup = lt::singleInstanceVerdict(owned);
        QVERIFY2(dup.message.contains(QStringLiteral("already running")),
                 "the duplicate case keeps the sentence users and e2e know");
        QVERIFY2(dup.message.contains(QStringLiteral("surface")),
                 "and it still promises the half that makes it true: the window came up");
        QVERIFY2(dup.duplicate && dup.exitCode == 0,
                 "a duplicate is not a failure: the user has a running Traduku");

        const QString said = lt::singleInstanceVerdict(
            QStringLiteral("no session bus: cannot guarantee a single instance")).message;
        QVERIFY2(!said.contains(QStringLiteral("already running")),
                 qPrintable(QStringLiteral("lied about a duplicate: ") + said));
        QVERIFY2(said.contains(QStringLiteral("session bus")),
                 qPrintable(QStringLiteral("did not pass the reason through: ") + said));

        const lt::SingleInstanceVerdict otherV =
            lt::singleInstanceVerdict(QStringLiteral("some other reason"));
        const QString other = otherV.message;
        QCOMPARE(otherV.exitCode, 1);
        QVERIFY2(!otherV.duplicate, "an unknown failure is not a duplicate");
        QVERIFY2(other.contains(QStringLiteral("some other reason")),
                 qPrintable(QStringLiteral("dropped the reason: ") + other));
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
