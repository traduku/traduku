/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include <QString>
#include <QStringList>

namespace lt {

class SoundPlayer;

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

/// True when this invocation was asked to print a report to the terminal:
/// every console mode, and `--selftest` (which needs widgets *and* prints).
/// The tray app is the one thing that is not, and the distinction is what
/// decideConsoleAttach() needs: on Windows a GUI-subsystem binary has no
/// console, and a user running `traduku --selftest` in cmd deserves more than
/// an exit code.
bool printsToTerminal(const QStringList &argv);

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
///
/// `qtCanPickPlatform` is the platform's answer, not a variable reading: the
/// default is the Unix one (DISPLAY/WAYLAND_DISPLAY), and the Windows main
/// passes its window-station fact instead. Passing the Unix reading on Windows
/// made every console mode force offscreen even on a logged-in desktop, which
/// then made `--backend-info` report `tray: none (no tray host)` on a machine
/// with a tray - a diagnostic that contradicts the product it describes.
void prepareCliEnvironment(const QStringList &argv,
                           bool qtCanPickPlatform = qtCanPickAPlatform());

/// True when the call above is what put QT_QPA_PLATFORM=offscreen there. The
/// backend report says so on its session line: "session: offscreen" read on
/// a box that has a desktop (ssh inherits no environment) tells the user
/// their session is gone, when what is gone is our knowledge of it.
bool qpaWasForcedByUs();

/*
 * Whether a console mode has to go and get the console it does not have.
 *
 * On Windows the tray binary is a GUI-subsystem executable (no black window
 * behind the tray), which also means it has no stdout: `traduku --translate`
 * from cmd would stream a translation into nothing. The answer is
 * AttachConsole(ATTACH_PARENT_PROCESS) - for the console modes only, and only
 * when the process does not already have a console (a pipe, or a real
 * terminal). Pure, so tests/unit/tst_appflavour.cpp pins the table; the call
 * itself is platform/win/WinConsole.cpp.
 */
enum class ConsoleDecision { Attach, LeaveAsIs };
ConsoleDecision decideConsoleAttach(bool onWindows, bool hasConsole,
                                   bool trayApp);

/*!
 * The line a Qt diagnostic becomes on the terminal.
 *
 * A GUI-subsystem binary has no console, and Qt's default message handler on
 * Windows notices that and routes qWarning() to OutputDebugString - invisible
 * unless a debugger is attached. Measured: `traduku --selftest` with no usable
 * desktop printed *nothing at all* and exited 2, because the advice it was
 * supposed to print went to a debugger nobody had open. The handler in
 * app/windows/main.cpp formats through this, and this is pure and tested
 * (tests/unit/tst_appflavour.cpp).
 */
QString qtConsoleMessage(const QString &type, const QString &text);

/*!
 * Why a QT_QPA_PLATFORM that names an absent plugin must be refused, in words.
 *
 * The rule is not "the user asked for it, so honour it": Qt answers an unknown
 * plugin name with a fatal message and, for a GUI-subsystem binary on Windows,
 * a modal dialog - a process that waits for a hand that is not there (a
 * scheduled task or an ssh session keeps one alive forever). So the refusal
 * happens before Qt is asked, and it says what was asked, what is missing, and
 * what this machine actually has.
 *
 * Pure: the caller collects the installed plugin names; this decides.
 */
QString platformPluginProblem(const QString &requestedQpa,
                              const QStringList &installedBaseNames);

/// What to tell the user - and the exit code 2 to use - when the mode they
/// asked for draws a window and this machine cannot give Qt a platform to do it
/// in. Empty when the mode can run.
///
/// `traduku --selftest` over plain ssh used to produce `Aborted (core dumped)`
/// and rc 134: QApplication reached Qt's createPlatformIntegration fatal, which
/// is a message about Qt rather than about Traduku, and nothing the user can
/// act on. A mode that needs a screen may refuse; it may not crash. The console
/// modes are not refused - they are what a user reaches for when the desktop is
/// broken - and a QT_QPA_PLATFORM of the user's own is treated as their answer.
/// @param onWindows  which OS's missing variables to name: "no DISPLAY" is
///                   correct on Linux and useless on Windows, where the
///                   equivalent is "no interactive desktop session".
/// Where a refusal of the GUI mode has to be put: the screen or the terminal.
///
/// A program started by double-click has no terminal, so writing a sentence to
/// stderr is writing it nowhere: the process exits 2 and the desktop shows
/// nothing at all - indistinguishable from a shortcut that was never clicked,
/// which is how the "double-click does nothing" bug survived long enough to
/// reach a person instead of a test log. A refusal is only a refusal if the
/// user can read it, so with no console it becomes a dialogue. With a console
/// it stays a line of text: putting a modal in front of someone reading a
/// terminal, or in front of a CI runner, would be worse than the silence.
bool guiRefusalNeedsDialog(bool consoleAvailable);

/// The tray's status line: the version, and which tray this is running as.
///
/// Built here rather than spelled out in each shell's main() because the
/// separator is where the bug was: a UTF-8 byte sequence written inside
/// QStringLiteral() arrives as one QChar per byte, so the middle dot became an
/// "A with circumflex" plus an invisible control, and the tray menu advertised a
/// broken build to every user who looked at it. One portable function, tested on
/// every platform, and no main() able to get it wrong again.
QString trayStatusLabel(const QString &version, const QString &trayKind);

/// What `--backend-info` says about sound, built for every shell at once.
///
/// Three copies of this block sat in three main()s, each asking about
/// SoundEvent::Finished alone. A profile whose three cues are answered by one
/// line cannot tell "the start cue is silent" apart from "the finish cue is
/// silent" - the pair the Windows hand walk of ROADMAP.md section 7.8 item 2
/// had to separate by ear. One function, the report on every shell, and no
/// main() able to print one cue and stay quiet about the other two.
struct SoundReport {
    QString summary;   ///< the "audio:" line: profile + the player that would run
    QStringList cues;  ///< one line per cue: "started: <file>", or the advice
};

/// The report for one player, asked - it resolves nothing new and plays
/// nothing, which is what makes it safe to call on the app's own player.
SoundReport soundReport(const SoundPlayer &player);


QString displayAdviceForMissingDisplay(const QStringList &argv,
                                       bool canPickPlatform, bool onWindows);

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
