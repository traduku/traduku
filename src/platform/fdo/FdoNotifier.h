#pragma once

#include "interfaces/inotifier.h"

#include <QDBusConnection>

namespace lt {

/*!
 * Desktop notifications through `org.freedesktop.Notifications` - the spec
 * every desktop implements (gnome-shell owns this name; Plasma has it too,
 * KNotification only adds categories on top). No KF6, no libnotify: it is
 * one D-Bus method with nine arguments.
 *
 * `bus` is injectable because a test cannot register the well-known name on
 * a real session bus - the desktop already owns it (see
 * tests/integration/fakebus.h).
 */
class FdoNotifier : public INotifier
{
    Q_OBJECT
public:
    explicit FdoNotifier(QDBusConnection bus = QDBusConnection::sessionBus());

    void notify(const QString &title, const QString &body,
                const QString &iconName = QString(), int ms = 0) override;
    QString backendName() const override { return QStringLiteral("fdo"); }

    /// Is anyone owning the service right now? --backend-info asks before
    /// promising the user that a bubble will appear.
    static bool isAvailable(const QDBusConnection &bus = QDBusConnection::sessionBus());

    /// Id the notification service assigned to the last bubble (0 = none
    /// yet). Proof of acceptance for --notify-test and the e2e suite: the
    /// message being sent is not the same as the daemon taking it.
    uint lastId() const { return m_lastId; }

    /// Last D-Bus problem (empty = none). Kept for --backend-info.
    QString errorText() const { return m_error; }

private:
    QDBusConnection m_bus;
    QString m_error;
    uint m_lastId = 0;
};

} // namespace lt
