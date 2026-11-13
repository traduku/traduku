/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinSessionInfo.h"

#include <QStringList>

namespace lt {

QString winSessionString(const WinSessionFacts &facts)
{
    // One line, and it names the consequence and not only the fact: a report
    // that says "session 0" makes the user look up what session 0 means, while
    // "no keyboard to observe, so the gesture cannot fire" is the answer they
    // were looking for. (Same rule as the udev advice on Linux.)
    QStringList parts;
    if (!facts.interactiveDesktop) {
        parts << QStringLiteral("no interactive desktop - no keyboard to observe, "
                               "so the gesture cannot fire");
    } else {
        if (facts.remoteSession) {
            parts << QStringLiteral("remote session");
        }
        if (!facts.desktopName.isEmpty()) {
            parts << QStringLiteral("desktop %1").arg(facts.desktopName);
        }
    }
    if (facts.sessionId != 0) {
        parts << QStringLiteral("session %1").arg(facts.sessionId);
    }
    if (!facts.qpaName.isEmpty() && facts.qpaName != QLatin1String("windows")) {
        // The QPA is what Qt was told to use, not what the machine is: CI and
        // a headless --backend-info force offscreen, and the report says so
        // instead of letting "offscreen" answer the question (SessionInfo.h).
        parts << QStringLiteral("qpa=%1").arg(facts.qpaName);
    }
    return parts.isEmpty() ? QStringLiteral("windows")
                           : QStringLiteral("windows (%1)").arg(parts.join(QStringLiteral(", ")));
}

} // namespace lt
