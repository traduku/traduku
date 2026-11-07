#pragma once

#include <QDBusConnection>
#include <QProcess>
#include <QString>

namespace test {

/// A private session bus for tests.
///
/// The real session bus cannot be used to test D-Bus *services*: well-known
/// names like org.freedesktop.Notifications are already owned by the desktop
/// (GNOME's notification daemon), so the test could not register its own.
/// Spawning `dbus-daemon --session` per test gives a bus nobody else is on;
/// it also makes the test independent of whether ctest runs inside a
/// graphical session at all.
class PrivateBus
{
public:
    bool start()
    {
        m_proc.setProgram(QStringLiteral("dbus-daemon"));
        m_proc.setArguments({QStringLiteral("--session"),
                             QStringLiteral("--print-address=1"),
                             QStringLiteral("--nopidfile")});
        m_proc.start();
        if (!m_proc.waitForStarted(3000)) {
            return false;                       // dbus-daemon not installed
        }
        if (!m_proc.waitForReadyRead(5000)) {
            return false;
        }
        m_address = QString::fromUtf8(m_proc.readLine().trimmed());
        if (!m_address.startsWith(QLatin1String("unix:"))) {
            return false;
        }
        // A named private connection (Qt: connectToBus(address, name)). A
        // private bus is the only way to own well-known names the real
        // desktop already holds - gnome-shell owns the notification service.
        m_name = QStringLiteral("traduku-test-bus-%1").arg(++s_counter);
        m_conn = new QDBusConnection(QDBusConnection::connectToBus(m_address, m_name));
        return m_conn->isConnected();
    }

    ~PrivateBus()
    {
        delete m_conn;
        if (!m_name.isEmpty()) {
            QDBusConnection::disconnectFromBus(m_name);
        }
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.terminate();
            if (!m_proc.waitForFinished(2000)) {
                m_proc.kill();
            }
        }
    }

    /// Another client of the same daemon. Two SingleInstance objects in one
    /// process must not share a connection: Qt lets one connection register
    /// the same well-known name twice, while the real second *process* has
    /// its own connection and gets a proper "already owned" error.
    QDBusConnection connect(const QString &name) const
    {
        return QDBusConnection::connectToBus(m_address, name);
    }

    QDBusConnection bus() const
    {
        return m_conn ? *m_conn : QDBusConnection::sessionBus();
    }
    QString address() const { return m_address; }

private:
    QProcess m_proc;
    QString m_address;
    QString m_name;
    inline static int s_counter = 0;   // unique connection names per process
    QDBusConnection *m_conn = nullptr;   // QDBusConnection is not default-constructible
};

} // namespace test
