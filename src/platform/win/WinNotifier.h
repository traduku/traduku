/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include "interfaces/inotifier.h"

#include <functional>

namespace lt {

/*!
 * Notifications on Windows.
 *
 * There is no `org.freedesktop.Notifications` here and no KNotification, and
 * the modern toast API wants an installer identity this project does not have
 * (ROADMAP.md §7.2, "вне объёма v1"). What a plain desktop app does have is a
 * balloon on its tray icon - so that is the channel, and the sender is
 * injected for the usual reason: the interesting assertions are about what the
 * app says when the balloon *cannot* be shown, and a test cannot arrange for
 * the shell to refuse.
 *
 * Counting delivered vs dropped is the point of the class. Sound and
 * notifications are the two features whose failure the user cannot see: the
 * translation still finishes, nothing chirps, nothing pops, and "the desktop
 * refused", "there is no host" and "we never asked" look identical from
 * outside. --backend-info prints both numbers.
 */
class WinNotifier : public INotifier
{
public:
    /// Shows the balloon; false = the desktop refused it (and errorText()
    /// after the call explains who).
    using Sender = std::function<bool(const QString &title, const QString &body,
                                      int ms)>;

    explicit WinNotifier(Sender sender = {}, QObject *parent = nullptr);

    void notify(const QString &title, const QString &body,
                const QString &iconName = QString(), int ms = 0) override;
    QString backendName() const override;

    QString errorText() const { return m_error; }
    /// The last failure, kept after a success: "no notifications since
    /// lunch" is a question a later success must not erase.
    QString lastError() const { return m_lastError; }
    quint64 delivered() const { return m_delivered; }
    quint64 dropped() const { return m_dropped; }

    /// The default channel: a balloon on a tray icon this object owns.
    /// Split out so a test can substitute its own record of what was asked
    /// for, and so the shell can share its QtTray icon instead (the balloon
    /// is only drawn by a host that has an icon to draw it on).
    static Sender trayBalloonSender(QObject *parent = nullptr);

private:
    Sender m_sender;
    QString m_error;
    QString m_lastError;
    quint64 m_delivered = 0;
    quint64 m_dropped = 0;
};

} // namespace lt
