#include "SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace lt {

namespace {

/// dev.traduku.Traduku - a reverse-DNS name, no org.kde.* outside the KDE
/// shell (ROADMAP.md §2).
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
    return QStringLiteral("%1-%2").arg(id, userToken());
}

} // namespace

QString userToken()
{
#ifdef Q_OS_UNIX
    return QString::number(static_cast<qulonglong>(::getuid()));
#else
    // The pipe namespace is per-session on Windows, but a shared name across
    // two users of the same console session would make one of them "already
    // running", so the name stays per-user wherever a user identity exists.
    const QString name = qEnvironmentVariable("USERNAME")
        .isEmpty() ? qEnvironmentVariable("USER") : qEnvironmentVariable("USERNAME");
    return name.isEmpty() ? QStringLiteral("default") : name.toLower();
#endif
}

SingleInstanceToken chooseSingleInstanceToken(bool onWindows, bool haveSessionBus)
{
    // No bus on Windows, so the claim has to come from the OS - just not from
    // the pipe, which any number of processes can listen on (measured in
    // tst_singleinstance; see interfaces/iinstancetoken.h).
    if (onWindows) {
        return SingleInstanceToken::LocalSocket;
    }
    Q_UNUSED(haveSessionBus)
    // On Unix the bus name stays the token even when there is no bus, because
    // the alternative is to *succeed* without a token - which is two trays,
    // and two trays is why this class exists. The caller gets the failure and
    // prints it (singleInstanceVerdict).
    return SingleInstanceToken::BusName;
}

SingleInstance::SingleInstance(const QString &id, QObject *parent)
    : SingleInstance(id, chooseSingleInstanceToken(
                             /*onWindows=*/
#ifdef Q_OS_WIN
                             true
#else
                             false
#endif
                             , QDBusConnection::sessionBus().isConnected()),
                     parent)
{
}

SingleInstance::SingleInstance(const QString &id, SingleInstanceToken token,
                               QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_busName(busNameFor(id))
    , m_socketName(socketNameFor(id))
    , m_token(token)
{
}

SingleInstance::SingleInstance(const QString &id, InstanceTokenFactory factory,
                               QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_busName(busNameFor(id))
    , m_socketName(socketNameFor(id))
    , m_token(SingleInstanceToken::LocalSocket)
    , m_tokenFactory(std::move(factory))
{
}

SingleInstance::~SingleInstance()
{
    stop();
}

void SingleInstance::stop()
{
    if (m_ownsName && m_bus && m_bus->isConnected()) {
        m_bus->unregisterService(m_busName);
    }
    if (m_server) {
        m_server->close();
    }
    if (m_tokenHandle) {
        m_tokenHandle->release();
        m_tokenHandle.reset();
    }
    m_ownsName = false;
}

bool SingleInstance::start(QDBusConnection bus)
{
    m_error.clear();
    if (m_token == SingleInstanceToken::LocalSocket) {
        if (!m_tokenFactory) {
            m_tokenFactory = defaultManagerTokenFactory();
        }
        return startOnToken();
    }
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

bool SingleInstance::startOnToken()
{
    // Without a mechanism there is no honest answer to "am I the only one",
    // and the answer the user deserves is a refusal plus a reason - not a
    // second tray that only shows up as a bug report.
    if (!m_tokenFactory) {
        m_error = QStringLiteral("no single-instance token was available for %1: "
                                 "refusing to start twice").arg(m_busName);
        return false;
    }
    m_tokenHandle = m_tokenFactory(m_socketName);
    if (!m_tokenHandle || !m_tokenHandle->acquire()) {
        // "already owned by a running Traduku" is the sentence the verdict
        // function recognises for both tokens; changing it silently turns a
        // refusal into "started, but not alone" in the shell's log.
        m_error = QStringLiteral("%1 is already owned by a running Traduku: %2")
                      .arg(m_busName,
                           m_tokenHandle ? m_tokenHandle->errorText()
                                         : QStringLiteral("the exclusivity "
                                                          "token could not be "
                                                          "created"));
        // Same second half as the bus path: refuse, and wake the one that has
        // it. A refused launch that stays silent is a double-click the user
        // will repeat four times.
        QLocalSocket socket;
        socket.connectToServer(m_socketName);
        if (socket.waitForConnected(1000)) {
            socket.write("activate");
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
        }
        m_tokenHandle.reset();
        return false;
    }

    // We own the claim, so the channel is ours to clean: a previous crash
    // leaves the socket file behind, and removing it now cannot steal another
    // instance's - the token is what says there is no other instance.
    QLocalServer::removeServer(m_socketName);
    m_server = new QLocalServer(this);
    if (!m_server->listen(m_socketName)) {
        // Not fatal: the token is held, so duplicates are still refused; only
        // the "wake up" message is lost, and that is worth saying out loud.
        m_error = QStringLiteral("running, but the activation channel %1 could "
                                 "not be opened: %2")
                      .arg(m_socketName, m_server->errorString());
    }
    m_ownsName = true;
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *conn = m_server->nextPendingConnection()) {
            conn->deleteLater();
            Q_EMIT activateRequested();
        }
    });
    return true;
}

} // namespace lt
