/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include <QStringList>

namespace lt {

/*!
 * Which QApplication flavour an invocation needs, and what the environment
 * has to look like before the app object is constructed.
 *
 * This lives in app/ rather than in each shell's main() for the reason the
 * project keeps rediscovering: the two shells each had their own copy of the
 * rule, and only one of them grew the headless branch. `traduku
 * --backend-info` - the command in the support README - aborted with
 * "no platform plugin could be initialized" on any box without a display,
 * which the test suite missed by always exporting QT_QPA_PLATFORM=offscreen.
 *
 * CLI modes run as a QGuiApplication, never a bare QCoreApplication: the
 * backend report and the clipboard factory speak QClipboard and
 * QGuiApplication::platformName(), both of which require a QGuiApplication
 * to exist. Offscreen satisfies that without a display; QCoreApplication
 * would turn those calls into a qFatal.
 */

/// True when this invocation draws a window (the tray app, or --selftest,
/// which screenshots itself). False for the console modes.
bool needsWidgets(const QStringList &argv);

/// True if Qt can choose a platform integration on its own, which means
/// WAYLAND_DISPLAY or DISPLAY in this process's environment. Deliberately not
/// "can we reach an X server at all": the clipboard finds XWayland through
/// /proc even over ssh, while the xcb plugin only reads the environment, and
/// treating the two as one question is what made --backend-info abort.
bool qtCanPickAPlatform();

/// The decision behind prepareCliEnvironment(), as a value: whether to leave
/// the QPA alone or to choose offscreen. Tested as a table instead of by
/// mutating the environment of the test process - a process-level flag asked
/// about after another call had already set it is how the first version of
/// this test failed on itself.
enum class QpaDecision
{
    LeaveAsIs,        ///< a GUI mode, a human chose the QPA, or Qt can pick one
    ForceOffscreen,   ///< console mode, no QPA set, nothing for Qt to pick
};
QpaDecision decideQpa(const QStringList &argv, bool qpaSetByHuman, bool qtCanPickPlatform);

/// Give a console mode a QPA when the machine has none. Does nothing for GUI
/// modes - the tray app with no desktop is a broken desktop, not a headless
/// tool, and must fail visibly rather than run invisibly. Does nothing when
/// QT_QPA_PLATFORM is already set: a human's choice outranks our guess.
void prepareCliEnvironment(const QStringList &argv);

/// True when the call above is what put QT_QPA_PLATFORM=offscreen there. The
/// backend report says so on its session line: "session: offscreen" read on
/// a box that has a desktop (ssh inherits no environment) tells the user
/// their session is gone, when what is gone is our knowledge of it.
bool qpaWasForcedByUs();

} // namespace lt
