/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinNotifier.h"

#include <QSystemTrayIcon>

namespace lt {

WinNotifier::WinNotifier(Sender sender, QObject *parent)
    : INotifier(parent)
    , m_sender(std::move(sender))
{
}

void WinNotifier::notify(const QString &title, const QString &body,
                         const QString &iconName, int ms)
{
    Q_UNUSED(iconName);   // a balloon has no icon argument; the tray icon is it
    if (!m_sender) {
        m_error = QStringLiteral("no notification channel: no tray host is "
                                 "showing an icon, so there is nothing to put "
                                 "a balloon on");
        m_lastError = m_error;
        ++m_dropped;
        return;
    }
    if (m_sender(title, body, ms)) {
        m_error.clear();
        ++m_delivered;
        return;
    }
    // The shell said no. Say it back, and count it: a notifier that "sent"
    // a refused balloon is how --backend-info ends up lying about a machine.
    m_error = QStringLiteral("the desktop refused the balloon (focus assist, "
                             "or a session with no shell to draw it)");
    m_lastError = m_error;
    ++m_dropped;
}

QString WinNotifier::backendName() const
{
    // A claim about this machine, printed before anything has been notified:
    // "none" must be visible in --backend-info, not inferred from the absence
    // of a line (an absent line is also what a bug looks like).
    return m_sender ? QStringLiteral("tray balloon (Shell_NotifyIcon)")
                    : QStringLiteral("none (no tray host for a balloon)");
}

WinNotifier::Sender WinNotifier::trayBalloonSender(QObject *parent)
{
    // The default channel owns its icon, because the alternative - silently
    // dropping every notification until somebody wires a tray - is the kind of
    // failure the counters above exist to make visible.
    auto *icon = new QSystemTrayIcon(parent);
    return [icon](const QString &title, const QString &body, int ms) {
        if (!QSystemTrayIcon::isSystemTrayAvailable()
            || !QSystemTrayIcon::supportsMessages()) {
            return false;
        }
        if (!icon->isVisible()) {
            icon->show();
        }
        icon->showMessage(title, body, QSystemTrayIcon::Information,
                          ms > 0 ? ms : 5000);
        // showMessage() has no return value on any platform; this is as close
        // to "the shell accepted it" as Qt lets us get, and the counters in
        // WinNotifier are what makes the difference visible to a user.
        return true;
    };
}

} // namespace lt
