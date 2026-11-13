/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include <QString>

namespace lt {

/*!
 * The facts about the Windows session, gathered by the OS and printed by
 * --backend-info.
 *
 * On Linux "session" answers one question (wayland or x11, which clipboard and
 * which listener follow from it). On Windows it answers a second, sharper one:
 * is there an interactive desktop here at all. A Traduku started as a service
 * or by a scheduled task in session 0 has no keyboard to observe, and a report
 * that stays silent about that leaves the user deducing it from a missing tray
 * icon.
 *
 * The struct is written by the test and read by the product; the collector in
 * WinSessionFacts.cpp is the only thing that asks the OS.
 */
struct WinSessionFacts
{
    bool interactiveDesktop = false;   ///< not session 0, and a desktop object exists
    bool remoteSession = false;        ///< RDP: latency, clipboard redirection, a different set of bugs
    quint32 sessionId = 0;
    QString desktopName;               ///< usually "Default" on Winsta0
    QString qpaName;                   ///< QGuiApplication::platformName(), for contrast only
};

/// "windows", "windows (remote session, desktop Default)", or a sentence that
/// names both the fact and its consequence for the gesture.
QString winSessionString(const WinSessionFacts &facts);

/// Asks the OS. Returns defaults (non-interactive) when the calls refuse.
WinSessionFacts queryWinSessionFacts(const QString &qpaName = QString());

} // namespace lt
