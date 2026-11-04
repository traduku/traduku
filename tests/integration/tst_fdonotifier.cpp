#include <QtTest>
#include <QDBusConnection>
#include <QVariantMap>

#include "platform/fdo/FdoNotifier.h"
#include "fakebus.h"

using namespace lt;

namespace {

/// Records what the app actually put on the bus.
class FakeNotifications : public QObject
{
    Q_OBJECT
    // Qt exposes an adaptor under the class name unless told otherwise;
    // the caller addresses the spec interface.
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    using QObject::QObject;
    struct Call
    {
        QString app, icon, summary, body;
        QStringList actions;
        QVariantMap hints;
        uint replacesId = 0;
        int timeout = -12345;
    };
    QList<Call> calls;

    Q_INVOKABLE uint Notify(const QString &app, uint replacesId, const QString &icon,
                            const QString &summary, const QString &body,
                            const QStringList &actions, const QVariantMap &hints,
                            int timeout)
    {
        calls << Call{app, icon, summary, body, actions, hints, replacesId, timeout};
        return static_cast<uint>(calls.size());
    }
};

} // namespace

class TestFdoNotifier : public QObject
{
    Q_OBJECT
    test::PrivateBus m_bus;
    FakeNotifications *m_service = nullptr;

private slots:
    void initTestCase()
    {
        if (!m_bus.start()) {
            QSKIP("dbus-daemon not available - cannot test D-Bus services");
        }
        m_service = new FakeNotifications(this);
        QVERIFY(m_bus.bus().registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                                           m_service,
                                           QDBusConnection::ExportAllInvokables));
        QVERIFY(m_bus.bus().registerService(QStringLiteral("org.freedesktop.Notifications")));
    }

    void notifyCarriesAppNameSummaryAndBody()
    {
        m_service->calls.clear();
        FdoNotifier n(m_bus.bus());
        n.notify(QStringLiteral("Traduku"), QStringLiteral("Clipboard is empty."));
        // Notify is fire-and-forget (a notification must never delay the
        // translation), so the assertion has to wait for delivery.
        QTRY_VERIFY_WITH_TIMEOUT(!m_service->calls.isEmpty()
                                 || !n.errorText().isEmpty(), 5000);
        QVERIFY2(!m_service->calls.isEmpty(), qPrintable(QStringLiteral("err=") + n.errorText()));
        const auto &c = m_service->calls.last();
        QCOMPARE(c.summary, QStringLiteral("Traduku"));
        QCOMPARE(c.body, QStringLiteral("Clipboard is empty."));
        // The FDO app_name is what the shell shows as the source of the
        // bubble; the .desktop Name is "Traduku".
        QCOMPARE(c.app, QStringLiteral("Traduku"));
        QVERIFY2(c.actions.isEmpty(), "an actionless bubble must not request buttons");
    }

    void iconIsPassedAsAppIcon()
    {
        m_service->calls.clear();
        FdoNotifier n(m_bus.bus());
        n.notify(QStringLiteral("t"), QStringLiteral("b"), QStringLiteral("edit-copy"));
        QTRY_VERIFY_WITH_TIMEOUT(!m_service->calls.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(m_service->calls.last().icon,
                                  QStringLiteral("edit-copy"), 5000);
    }

    void timeoutZeroMeansServerDefault()
    {
        m_service->calls.clear();
        FdoNotifier n(m_bus.bus());
        n.notify(QStringLiteral("t"), QStringLiteral("b"));
        QTRY_VERIFY_WITH_TIMEOUT(!m_service->calls.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(m_service->calls.last().timeout, 0, 5000);
        n.notify(QStringLiteral("t"), QStringLiteral("b"), QString(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(m_service->calls.last().timeout, 5000, 5000);
    }

    void lastIdIsTheIdTheServiceReturned()
    {
        // --notify-test and the e2e suite need proof that the daemon
        // ACCEPTED the bubble, not just that we sent a message.
        m_service->calls.clear();
        FdoNotifier n(m_bus.bus());
        QCOMPARE(n.lastId(), 0u);
        n.notify(QStringLiteral("t"), QStringLiteral("b"));
        QTRY_VERIFY_WITH_TIMEOUT(!m_service->calls.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(n.lastId(), 1u, 5000);   // fake returns # of calls
    }

    void backendNameIsStableForBackendInfo()
    {
        FdoNotifier n(m_bus.bus());
        QCOMPARE(n.backendName(), QStringLiteral("fdo"));
    }

    void missingServiceIsReportedNotCrashed()
    {
        test::PrivateBus empty;
        if (!empty.start()) {
            QSKIP("dbus-daemon not available");
        }
        QVERIFY(!FdoNotifier::isAvailable(empty.bus()));
        FdoNotifier n(empty.bus());
        n.notify(QStringLiteral("t"), QStringLiteral("b"));   // must not throw
        QVERIFY2(!n.errorText().isEmpty(), "the operator gets no reason");
        QVERIFY2(n.errorText().contains(QStringLiteral("org.freedesktop.Notifications")),
                 qPrintable(n.errorText()));
    }

    void availableWhenTheServiceIsRegistered()
    {
        QVERIFY(FdoNotifier::isAvailable(m_bus.bus()));
    }
};

QTEST_GUILESS_MAIN(TestFdoNotifier)
#include "tst_fdonotifier.moc"
