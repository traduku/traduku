#pragma once

#include <QDBusConnection>

#include <optional>

class QLocalServer;

namespace lt {

/*!
 * One Traduku per session (ROADMAP.md §2), implemented with plain Qt so
 * both shells share it - the KDE shell may keep using KDBusService, but the
 * GNOME one must not depend on KF6 for this.
 *
 * Two moving parts:
 *  - the session-bus name `dev.traduku.Traduku` is the ownership token (the
 *    .desktop advertises it, so a session manager activation also lands on
 *    the running instance);
 *  - a QLocalServer `traduku-<uid>` carries "surface yourself" from a
 *    refused second instance to the primary one.
 */
class SingleInstance : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstance(const QString &id = QStringLiteral("traduku"),
                            QObject *parent = nullptr);
    /// Releases the bus name, so a restart can take it.
    ~SingleInstance() override;

    /// false = somebody else owns it; that instance was asked to activate.
    bool start(QDBusConnection bus = QDBusConnection::sessionBus());

    QString busName() const { return m_busName; }
    QString errorText() const { return m_error; }

Q_SIGNALS:
    /// Emitted in the PRIMARY instance when another copy was launched.
    void activateRequested();

private:
    QString m_id;
    QString m_busName;
    QString m_socketName;
    // QDBusConnection has no default ctor and no usable assignment,
    // so the bus handed to start() is held in an optional.
    std::optional<QDBusConnection> m_bus;
    bool m_ownsName = false;
    QLocalServer *m_server = nullptr;
    QString m_error;
};

} // namespace lt
