/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinClipboardPolicy.h"

namespace lt {

QString windowsClipboardProblem(const QString &forced)
{
    const QString f = forced.trimmed().toLower();
    if (f.isEmpty() || f == QLatin1String("qt")) {
        return {};                       // nothing asked for, or the one reader there is
    }
    if (f == QLatin1String("wl")) {
        // Names the tool the user is holding a note about, and the reason it
        // has no target here: "unsupported" would be answered by installing
        // wl-clipboard, which is exactly the wrong errand on this OS.
        return QStringLiteral("TRADUKU_CLIPBOARD=wl could not be used: there is "
                              "no wl-paste/wl-copy on Windows - no compositor "
                              "owns a clipboard here");
    }
    if (f == QLatin1String("x11")) {
        return QStringLiteral("TRADUKU_CLIPBOARD=x11 could not be used: there is "
                              "no X server and no X selections on Windows");
    }
    return QStringLiteral("TRADUKU_CLIPBOARD=%1 is not a known backend here "
                          "(only qt)").arg(forced);
}

} // namespace lt
