#pragma once

#include "interfaces/iinstancetoken.h"

#include <QDBusConnection>

#include <functional>
#include <memory>
#include <optional>

class QLocalServer;

namespace lt {

/*!
 * What proves "there is already a Traduku". Two different OSes can answer
 * that, and only one of the answers is available on each.
 *
 *   BusName      - own `dev.traduku.Traduku` on the session bus (any Unix
 *                  desktop). Chosen on Linux even when the bus is *absent*:
 *                  the fallback would quietly allow two trays, and two trays
 *                  is the bug the token exists to prevent, so the honest
 *                  answer there is the failure the caller can print.
 *   Token        - an injected exclusivity claim plus a local channel. There
 *                  is no session bus on Windows to hold a name on, and a named
 *                  pipe turned out not to be exclusive (see
 *                  interfaces/iinstancetoken.h), so the claim and the channel
 *                  are two objects here rather than one lucky bind. The enum
 *                  name stayed LocalSocket because it is the *channel* a
 *                  refused copy dials; what makes it single is the token.
 */
enum class SingleInstanceToken { BusName, LocalSocket };

/// The decision, as a value, so the table is tested rather than trusted
/// (tests/integration/tst_singleinstance.cpp).
SingleInstanceToken chooseSingleInstanceToken(bool onWindows, bool haveSessionBus);

/// Per-user suffix for the token name: /tmp and the named-pipe namespace are
/// both shared, and two users may each run Traduku.
QString userToken();

/// The mechanism this platform trusts to be exclusive (mutex on Windows,
/// socket bind on Unix). Defined once per OS in app/backends/.
InstanceTokenFactory defaultManagerTokenFactory();

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
    /// The token spelled out, for a test that wants the channel path on every
    /// platform, and for the Windows shell, which has no bus to ask.
    SingleInstance(const QString &id, SingleInstanceToken token,
                   QObject *parent = nullptr);
    /// With the exclusivity mechanism supplied by the caller (tests, and any
    /// shell that wants to say what "already running" was measured against).
    SingleInstance(const QString &id, InstanceTokenFactory factory,
                   QObject *parent = nullptr);
    /// Releases the bus name, so a restart can take it.
    ~SingleInstance() override;

    /// false = somebody else owns it; that instance was asked to activate.
    /// The bus is ignored by the LocalSocket token (it has nothing to do with
    /// D-Bus) - passing sessionBus() stays correct on both.
    bool start(QDBusConnection bus = QDBusConnection::sessionBus());

    /// Give up the claim without dying. The dtor does the same, and a test (or
    /// a restart path) that wants the name back *now* rather than at scope exit
    /// needs a verb for it - otherwise "the token is released on exit" is a
    /// claim nobody can check without a second process.
    void stop();

    SingleInstanceToken token() const { return m_token; }

    QString busName() const { return m_busName; }
    /// The activation channel's name: what the refused copy dials.
    QString socketName() const { return m_socketName; }
    QString errorText() const { return m_error; }

Q_SIGNALS:
    /// Emitted in the PRIMARY instance when another copy was launched.
    void activateRequested();

private:
    /// The non-D-B2 half of start(): acquire the token, then open the channel.
    bool startOnToken();

    QString m_id;
    QString m_busName;
    QString m_socketName;
    // QDBusConnection has no default ctor and no usable assignment,
    // so the bus handed to start() is held in an optional.
    std::optional<QDBusConnection> m_bus;
    SingleInstanceToken m_token;
    InstanceTokenFactory m_tokenFactory;
    std::unique_ptr<IInstanceToken> m_tokenHandle;
    bool m_ownsName = false;
    QLocalServer *m_server = nullptr;
    QString m_error;
};

} // namespace lt
