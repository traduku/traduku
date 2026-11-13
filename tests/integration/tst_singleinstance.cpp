#include <QtTest>
#include <QDBusConnection>
#include <QLocalServer>
#include <QSignalSpy>

#include "app/SingleInstance.h"

#include <QSet>
#include <memory>
#include "app/AppFlavour.h"
#include "fakebus.h"

using namespace lt;

class TestSingleInstance : public QObject
{
    Q_OBJECT
    test::PrivateBus m_bus;
    bool m_busOk = false;

private slots:
    void initTestCase()
    {
        // Started, but not fatal: a box without dbus-daemon must skip the
        // bus-backed rows and still run the portable ones below. Skipping the
        // whole binary because one platform's token is missing is how a
        // Windows build ends up shipping an untested single-instance path.
        m_busOk = m_bus.start();
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
        if (!m_busOk) {
            QSKIP("dbus-daemon not available");
        }
        SingleInstance s;
        QVERIFY2(s.start(m_bus.bus()), qPrintable(s.errorText()));
        QVERIFY(s.errorText().isEmpty());
    }

    void secondInstanceIsRefusedAndWakesTheFirst()
    {
        if (!m_busOk) {
            QSKIP("dbus-daemon not available");
        }
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
        if (!m_busOk) {
            QSKIP("dbus-daemon not available");
        }
        {
            SingleInstance holding;
            QVERIFY(holding.start(m_bus.bus()));
        }                                        // destructor releases the name
        SingleInstance next;
        QVERIFY2(next.start(m_bus.bus()), qPrintable(next.errorText()));
    }

    void noBusIsReportedNotIgnored()
    {
        if (!m_busOk) {
            QSKIP("dbus-daemon not available");
        }
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

    // ---- Windows: the token is a named pipe, not a bus name --------------

    void tokenChosenByPlatformNotByWish()
    {
        // There is no session bus on Windows, so the ownership token has to be
        // something the OS guarantees: a named pipe can only be created once.
        QCOMPARE(lt::chooseSingleInstanceToken(true, false),
                 SingleInstanceToken::LocalSocket);
        QCOMPARE(lt::chooseSingleInstanceToken(true, true),
                 SingleInstanceToken::LocalSocket);
        QCOMPARE(lt::chooseSingleInstanceToken(false, true),
                 SingleInstanceToken::BusName);
        // A Linux box with no session bus keeps the bus token and its honest
        // failure. Falling back to a pipe there would quietly allow two trays,
        // which is the exact thing the token exists to prevent.
        QCOMPARE(lt::chooseSingleInstanceToken(false, false),
                 SingleInstanceToken::BusName);
    }

    void localSocketTokenRefusesTheSecondCopyAndWakesTheFirst()
    {
        // An id of its own, and a cleared name first: this row must not be
        // poisoned by a socket a crashed earlier run left behind.
        QLocalServer::removeServer(QStringLiteral("traduku-token-test-") + lt::userToken());
        {
            SingleInstance first(QStringLiteral("traduku-token-test"),
                                 SingleInstanceToken::LocalSocket);
            QVERIFY2(first.start(), qPrintable(first.errorText()));
            QSignalSpy woke(&first, &SingleInstance::activateRequested);

            SingleInstance second(QStringLiteral("traduku-token-test"),
                                  SingleInstanceToken::LocalSocket);
            QVERIFY(!second.start());
            QVERIFY2(second.errorText().contains(QStringLiteral("already owned")),
                     qPrintable(second.errorText()));
            // One verdict function for both tokens: the shell's sentence must
            // not depend on which OS asked the question.
            const lt::SingleInstanceVerdict v =
                lt::singleInstanceVerdict(second.errorText());
            QVERIFY2(v.duplicate, qPrintable(v.message));
            QCOMPARE(v.exitCode, 0);
            QTRY_COMPARE_WITH_TIMEOUT(woke.count(), 1, 5000);
        }
        // Releasing it lets the next copy own it (the restart case: quit from
        // the tray, start again, and do not be told another copy is running).
        SingleInstance third(QStringLiteral("traduku-token-test"),
                             SingleInstanceToken::LocalSocket);
        QVERIFY2(third.start(), qPrintable(third.errorText()));
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
