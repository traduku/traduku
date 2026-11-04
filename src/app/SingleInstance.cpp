#include "SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>

#include <unistd.h>

namespace lt {

namespace {

/// dev.traduku.Traduku - a reverse-DNS name, no org.kde.* outside the KDE
/// shell (GNOME_PLAN.md D5).
QString busNameFor(const QString &id)
{
    QString pretty = id;
    if (!pretty.isEmpty()) {
        pretty[0] = pretty.at(0).toUpper();
    }
    return QStringLiteral("dev.traduku.%1").arg(pretty);
}

QString socketNameFor(const QString &id)
{
    // Per-user: /tmp is shared, and two users may both run Traduku.
    return QStringLiteral("%1-%2").arg(id).arg(::getuid());
}

} // namespace

SingleInstance::SingleInstance(const QString &id, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_busName(busNameFor(id))
    , m_socketName(socketNameFor(id))
{
}

SingleInstance::~SingleInstance()
{
    if (m_ownsName && m_bus && m_bus->isConnected()) {
        m_bus->unregisterService(m_busName);
    }
}

bool SingleInstance::start(QDBusConnection bus)
{
    m_error.clear();
    m_bus.emplace(std::move(bus));

    if (!m_bus->isConnected()) {
        m_error = QStringLiteral("no session bus: cannot guarantee a single "
                                 "instance (%1)").arg(m_busName);
        return false;
    }

    if (!m_bus->registerService(m_busName)) {
        m_error = QStringLiteral("%1 is already owned by a running Traduku")
                      .arg(m_busName);
        // Tell that one to surface its window; never block on it - a stuck
        // primary must not turn `traduku` into a hang.
        QLocalSocket socket;
        socket.connectToServer(m_socketName);
        if (socket.waitForConnected(1000)) {
            socket.write("activate");
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
        }
        return false;
    }

    m_ownsName = true;
    // A previous crash leaves the socket file behind and listen() then fails.
    QLocalServer::removeServer(m_socketName);
    m_server = new QLocalServer(this);
    if (!m_server->listen(m_socketName)) {
        // Not fatal: we still own the name, so duplicates are refused; only
        // the "wake up" message is lost.
        m_error = QStringLiteral("running, but the activation socket %1 could "
                                 "not be opened: %2")
                      .arg(m_socketName, m_server->errorString());
    }
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *conn = m_server->nextPendingConnection()) {
            conn->deleteLater();
            Q_EMIT activateRequested();
        }
    });
    return true;
}

} // namespace lt
