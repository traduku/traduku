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

namespace lt {

/*!
 * What the first launch of an installed app should do, decided in one place.
 *
 * A program whose only interface is a key gesture is invisible until someone
 * presses it, and the two desktops are invisible for different reasons. Plasma
 * has no packaged autostart, so the app must ASK (nothing may write an
 * autostart entry without a yes). GNOME ships /etc/xdg/autostart/traduku.desktop
 * in the package, so asking there is a question with an answer the user cannot
 * change - and its panel icon exists only with the appindicator extension, so
 * the greeting is the one moment a user learns that Ctrl+C twice is the whole
 * interface. Deciding it per shell is how one shell silently loses the
 * behaviour (the KDE first-run dialog was once the only one in the tree).
 */
struct FirstRunPlan
{
    bool greet = false;               // notify: what to press
    bool askAboutAutostart = false;   // modal yes/no, Plasma only
    bool explainMissingIcon = false;  // how to get a panel icon, GNOME only
    /// Put the settings dialog in front of the user. Plasma asks about
    /// autostart in a modal anyway, so it may as well finish the setup there.
    /// GNOME starts unattended at login, and a modal at login is how people
    /// learn to switch autostart off - so only when there is nothing to
    /// translate with yet.
    bool openSettings = false;
};

/// `shell` is "gnome" or "kde"; `trayAvailable` is ITray::isAvailable() as
/// measured at startup, not a guess about the desktop; `endpointConfigured`
/// is whether an API endpoint is set.
FirstRunPlan planFirstRun(const QString &shell, bool trayAvailable,
                          bool endpointConfigured = true);

/*!
 * `traduku --autostart=on|off|status`, in one place. It drives the same
 * IAutostart the tray menu drives rather than writing the file itself: two
 * implementations of "start at login" is how a menu and a command end up
 * disagreeing about the answer.
 */
int runAutostartCommand(const QString &action);

/*!
 * The application's XDG base name - "traduku" - which is the autostart file
 * name, the .desktop file name without suffix, the WM_CLASS Qt derives from
 * applicationName, and the value StartupWMClass must carry. Four spellings of
 * one identity in three places (code, packaging/gnome/traduku.desktop, the
 * session's window matching); every one of them written out by hand is a
 * mismatch waiting to happen - without StartupWMClass the launcher icon and
 * the window are two entries in the dash, which is the complaint GNOME users
 * file first.
 */
QString desktopBaseName();

/*!
 * What the shell decided at startup, in the shape the journal line must have.
 *
 * The E2E harness used to wait for the app with `sleep 4` - four seconds of
 * every test, on both desktops, and still red when a VM was loaded. The
 * product knows exactly when it finished starting up, so it says so in one
 * line and the harness waits for that sentence: faster, and a startup that
 * hangs now fails with the reason instead of with an empty screenshot.
 *
 * Pure, so the format - a single line, `key=value` fields - is testable
 * (tests/unit/tst_appflavour) rather than something the harness learns by
 * timing out.
 */
struct ReadyInfo
{
    QString shell;              ///< "gnome" | "kde"
    QString session;            ///< sessionType(), e.g. "wayland"
    QString clipboard;          ///< IClipboard::backendName()
    QString hotkey;             ///< IHotkeyBackend::backendName(), empty = none
    bool trayAvailable = false;
};

/// "traduku: ready (gnome, wayland; clipboard=x11 hotkey=x11-record tray=yes)"
QString readyLine(const ReadyInfo &info);

/// qInfo() of readyLine(), split out so both shells log the same sentence.
void logReady(const ReadyInfo &info);

/*!
 * What to tell the user when SingleInstance::start() refused.
 *
 * "Traduku is already running" is true for exactly one of start()'s failures -
 * the bus name being owned. The shells used to print it for all of them, so a
 * bus-less `traduku` (env -i, ssh, a broken session) told its user to go look
 * for a copy that was never there. The reason comes from the object that
 * knows it; the rule over which sentence to print lives here, once, so the two
 * shells cannot disagree (tests/integration/tst_singleinstance.cpp).
 */
struct SingleInstanceVerdict
{
    QString message;   ///< what to print, with the reason that was measured
    int exitCode = 0;  ///< 0 for a duplicate (nothing went wrong), 1 otherwise
    bool duplicate = false;
};

SingleInstanceVerdict singleInstanceVerdict(const QString &errorText);

} // namespace lt
