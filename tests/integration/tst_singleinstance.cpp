#include <QtTest>
#include <QDBusConnection>
#include <QSignalSpy>

#include "app/SingleInstance.h"
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
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
