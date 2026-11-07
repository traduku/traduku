/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "AppFlavour.h"


#include <QByteArray>
#include <QStringList>

namespace lt {

namespace {

/// Modes that never open a window and never live in the tray. Kept here
/// rather than per shell: two lists drifted once already (the KDE shell had
/// no headless path at all, so `--backend-info` aborted there).
const QStringList &cliOnlyFlags()
{
    static const QStringList flags = {
        QStringLiteral("--translate"), QStringLiteral("-t"),
        QStringLiteral("--text"),
        QStringLiteral("--backend-info"),
        QStringLiteral("--notify-test"),
        QStringLiteral("--help"), QStringLiteral("-h"),
        QStringLiteral("--version"),
    };
    return flags;
}

} // namespace

bool needsWidgets(const QStringList &argv)
{
    // --selftest is the CLI mode that shows a window (it screenshots itself),
    // so it is deliberately NOT in cliOnlyFlags: it needs widgets and a QPA.
    for (const QString &a : argv) {
        if (cliOnlyFlags().contains(a)) {
            return false;
        }
    }
    return true;   // no mode flag: the tray app
}

bool qtCanPickAPlatform()
{
    // Only what Qt itself can see. The clipboard may still reach the session's
    // XWayland through /proc discovery (X11Clipboard does exactly that), but
    // the xcb plugin reads DISPLAY from the environment and nothing else -
    // "we can reach X" and "Qt can start" are two questions, and answering the
    // first for both left --backend-info aborting over ssh.
    if (!qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty()) {
        return true;
    }
    if (!qEnvironmentVariable("DISPLAY").isEmpty()) {
        return true;    // set but broken: Qt's own error is the honest one
    }
    return false;
}

namespace {
bool g_forcedOffscreen = false;
}

bool qpaWasForcedByUs()
{
    return g_forcedOffscreen;
}

QpaDecision decideQpa(const QStringList &argv, bool qpaSetByHuman, bool qtCanPickPlatform)
{
    // Only the console modes get the rescue. The tray app with no display is
    // not a headless tool, it is a desktop that is not up, and letting it run
    // silently is how a missing icon stays unexplained.
    if (needsWidgets(argv)) {
        return QpaDecision::LeaveAsIs;
    }
    // A human's QT_QPA_PLATFORM outranks our guess, broken as it may be:
    // silently overriding it would make the failure impossible to reproduce.
    if (qpaSetByHuman) {
        return QpaDecision::LeaveAsIs;
    }
    if (qtCanPickPlatform) {
        return QpaDecision::LeaveAsIs;
    }
    return QpaDecision::ForceOffscreen;
}

void prepareCliEnvironment(const QStringList &argv)
{
    const QpaDecision decision = decideQpa(argv, qEnvironmentVariableIsSet("QT_QPA_PLATFORM"),
                                            qtCanPickAPlatform());
    if (decision != QpaDecision::ForceOffscreen) {
        return;
    }
    // No display, no wayland socket, no XWayland to find: Qt's own platform
    // selection would qFatal("no platform plugin could be initialized") and
    // abort. --backend-info is the command a user reaches for precisely when
    // the desktop is not working, so "it aborts when there is no desktop"
    // makes the diagnostic useless exactly where it is needed.
    //
    // Offscreen rather than a plain QCoreApplication: the reports and the
    // clipboard factory speak QClipboard and QGuiApplication::platformName(),
    // both of which require a QGuiApplication - see AppFlavour.h.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    g_forcedOffscreen = true;
}

} // namespace lt
