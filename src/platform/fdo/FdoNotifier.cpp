#include "FdoNotifier.h"

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QVariantMap>

namespace lt {

namespace {
constexpr auto Service = "org.freedesktop.Notifications";
constexpr auto Path = "/org/freedesktop/Notifications";
constexpr auto Interface = "org.freedesktop.Notifications";
/// Shown as the source of the bubble; matches the .desktop Name, not the
/// binary name.
constexpr auto AppName = "Traduku";
} // namespace

FdoNotifier::FdoNotifier(QDBusConnection bus)
    : INotifier(nullptr)
    , m_bus(std::move(bus))
{
}

bool FdoNotifier::isAvailable(const QDBusConnection &bus)
{
    if (!bus.isConnected()) {
        return false;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameHasOwner"));
    msg << QString::fromLatin1(Service);
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 2000);
    return reply.type() != QDBusMessage::ErrorMessage
        && reply.arguments().value(0).toBool();
}

void FdoNotifier::notify(const QString &title, const QString &body,
                         const QString &iconName, int ms)
{
    m_error.clear();
    m_lastId = 0;

    if (!m_bus.isConnected()) {
        m_error = QStringLiteral("no session bus: cannot reach ")
                  + QString::fromLatin1(Service);
        return;
    }
    // Cheap check first (~1 ms) so a machine without any notification
    // service fails synchronously instead of hanging a call nobody waits on.
    if (!isAvailable(m_bus)) {
        m_error = QStringLiteral("nobody owns ") + QString::fromLatin1(Service)
                  + QStringLiteral(" - no notification service is running");
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(Service), QString::fromLatin1(Path),
        QString::fromLatin1(Interface), QStringLiteral("Notify"));
    // app_name, replaces_id, app_icon, summary, body, actions, hints, timeout
    msg << QString::fromLatin1(AppName) << 0u << iconName << title << body
        << QStringList{} << QVariantMap{} << ms;

    // Async on purpose: a notification must never delay the translation
    // path, and a wedged daemon must not freeze the UI.
    QDBusPendingCall pending = m_bus.asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                if (w->isError()) {
                    m_error = w->error().message();
                } else {
                    const QDBusMessage reply = w->reply();
                    if (!reply.arguments().isEmpty()) {
                        m_lastId = reply.arguments().at(0).toUInt();
                    }
                }
                w->deleteLater();
            });
}

} // namespace lt
