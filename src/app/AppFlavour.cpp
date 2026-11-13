/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "AppFlavour.h"

#include "app/PlatformBackends.h"
#include "core/soundplayer.h"
#include "interfaces/iautostart.h"


#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QTextStream>

namespace lt {

namespace {

/// Modes that never open a window and never live in the tray. Kept here
/// rather than per shell: two lists drifted once already (the KDE shell had
/// no headless path at all, so `--backend-info` aborted there).
/// Modes that never open a window and never live in the tray.
const QStringList &cliOnlyFlags()
{
    static const QStringList flags = {
        QStringLiteral("--translate"), QStringLiteral("-t"),
        QStringLiteral("--text"),
        QStringLiteral("--backend-info"),
        QStringLiteral("--notify-test"),
        QStringLiteral("--autostart"),
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
        // Compare the option, not the whole token: users write
        // --autostart=off, and matching the full string made that form look
        // like "no mode flag" - QApplication, then qFatal with no display.
        const QString name = a.section(QLatin1Char('='), 0, 0);
        if (cliOnlyFlags().contains(name)) {
            return false;
        }
    }
    return true;   // no mode flag: the tray app
}

const QStringList &reportingFlags()
{
    // Modes that print a report to the terminal *and* may build a window.
    // They are not in cliOnlyFlags (--selftest screenshots itself, so it needs
    // widgets), and they are not the tray app either: `traduku --selftest` is
    // the command a user runs in a terminal to check an install, and on Windows
    // a GUI-subsystem binary writes nothing there unless it asks for the
    // console. Forgetting them is how --selftest becomes a silent exit code.
    static const QStringList flags = {QStringLiteral("--selftest"),
                                      QStringLiteral("--live")};
    return flags;
}

bool printsToTerminal(const QStringList &argv)
{
    for (const QString &a : argv) {
        const QString name = a.section(QLatin1Char('='), 0, 0);
        if (cliOnlyFlags().contains(name) || reportingFlags().contains(name)) {
            return true;
        }
    }
    return false;
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

QString trayStatusLabel(const QString &version, const QString &trayKind)
{
    // Written as an escape in the source's own alphabet, not as UTF-8 bytes:
    // QStringLiteral() builds UTF-16 from the *bytes* of the literal, so a
    // hand-written "\xC2\xB7" arrives as two characters (U+00C2, U+00B7) and the
    // tray shows the first of them. An escape Qt and the compiler agree on
    // cannot rot that way.
    return QStringLiteral("Traduku %1 \u00B7 tray %2").arg(version, trayKind);
}

bool guiRefusalNeedsDialog(bool consoleAvailable)
{
    return !consoleAvailable;
}

SoundReport soundReport(const SoundPlayer &player)
{
    SoundReport r;
    const SoundPlayer::Profile &profile = player.profile();

    // The user's switch first: it is one sentence, and three lines of advice
    // about a feature somebody turned off is how the advice that is true gets
    // skipped.
    const SoundPlayer::Status start = player.status(SoundEvent::Started);
    if (!start.enabled) {
        r.summary = QStringLiteral("off (Sounds.Enabled=false)");
        return r;
    }

    // The summary names the profile and the player that would run: "which
    // sound system is this" is a question about the machine, and the answer
    // comes from the object that plays.
    r.summary = QStringLiteral("%1: %2").arg(
        profile.name,
        start.player.isEmpty() ? QStringLiteral("no player") : start.player);

    // One line per cue. A report that prints one cue for a three-cue profile
    // cannot answer "which of the two is the silent one", and that is the
    // question a user with no sound came here to ask.
    const struct { SoundEvent event; const char *name; } cues[] = {
        {SoundEvent::Started, "started"},
        {SoundEvent::Finished, "finished"},
        {SoundEvent::Failed, "failed"},
    };
    for (const auto &cue : cues) {
        const SoundPlayer::Status st = player.status(cue.event);
        r.cues << QStringLiteral("%1: %2").arg(
            QString::fromUtf8(cue.name),
            st.problem.isEmpty() ? st.file : st.problem);
    }
    return r;
}

QString displayAdviceForMissingDisplay(const QStringList &argv, bool canPickPlatform,
                                      bool onWindows)
{
    if (canPickPlatform || !needsWidgets(argv)) {
        return QString();
    }
    // A QPA the human set is their answer to the question - offscreen, vnc,
    // whatever they chose. Only the case where nobody said anything is ours to
    // explain. Note this must be consulted AFTER prepareCliEnvironment(): the
    // console modes get offscreen from us, and qpaWasForcedByUs() distinguishes
    // that from a human's choice.
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        return QString();
    }
    // The sentence names the variables that are missing *here*: telling a
    // Windows user to export DISPLAY is how a refusal becomes a bug report.
    const QString why = onWindows
        ? QStringLiteral("no interactive desktop session - a service, a "
                         "scheduled task, or a disconnected remote session")
        : QStringLiteral("no DISPLAY, no WAYLAND_DISPLAY");
    return QStringLiteral(
        "traduku: this mode opens a window, and there is no display Qt can open "
        "one on (%1). Run it from your desktop session, or set "
        "QT_QPA_PLATFORM=offscreen to exercise it without a screen. "
        "traduku --backend-info answers without any display.").arg(why);
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

FirstRunPlan planFirstRun(const QString &shell, bool trayAvailable,
                          bool endpointConfigured)
{
    FirstRunPlan plan;
    plan.greet = true;
    // Which shells have to *ask* about autostart: the ones that installed
    // nothing. Plasma ships no autostart entry, and the Windows build ships no
    // package at all - so nothing may write the Run key without a yes, while
    // the GNOME package already put its entry in /etc/xdg/autostart and asking
    // there is a question with an answer the user cannot change.
    const bool installsAutostart = (shell == QLatin1String("gnome"));
    plan.askAboutAutostart = !installsAutostart;
    plan.explainMissingIcon = !trayAvailable;
    // KDE has always opened Settings on the first run (it is asking about
    // autostart in a modal anyway); everywhere else it is opened only when
    // there is nothing to translate with yet - an unattended login with a
    // modal is how people learn to switch autostart off.
    plan.openSettings = (shell == QLatin1String("kde")) ? true : !endpointConfigured;
    return plan;
}

void prepareCliEnvironment(const QStringList &argv, bool qtCanPickPlatform)
{
    const QpaDecision decision = decideQpa(argv, qEnvironmentVariableIsSet("QT_QPA_PLATFORM"),
                                            qtCanPickPlatform);
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


QString platformPluginProblem(const QString &requestedQpa,
                              const QStringList &installedBaseNames)
{
    // "windows:darkmode=1" is one platform with an option, so the name is what
    // has to exist - comparing the whole string would refuse a valid setting.
    const QString name = requestedQpa.section(QLatin1Char(':'), 0, 0).trimmed();
    if (name.isEmpty()) {
        return {};    // nobody named one; the platform picks its own default
    }
    for (const QString &p : installedBaseNames) {
        if (p.compare(name, Qt::CaseInsensitive) == 0) {
            return {};
        }
    }
    if (installedBaseNames.isEmpty()) {
        // A different sentence: "not among these" over an empty list reads like
        // a bug in the report rather than a broken install.
        return QStringLiteral(
            "traduku: there are no platform plugins installed at all, so "
            "QT_QPA_PLATFORM=%1 cannot be honoured and Qt would die asking. "
            "This looks like an incomplete package (a stripped windeployqt "
            "run, or the wrong directory was shipped)").arg(name);
    }
    return QStringLiteral(
        "traduku: QT_QPA_PLATFORM=%1 names a platform that is not installed "
        "here; this machine has: %2. Qt refuses to start without a platform "
        "and a GUI binary would only put up a dialog nobody can click, so "
        "nothing was attempted.").arg(name, installedBaseNames.join(
                                            QStringLiteral(", ")));
}

QString qtConsoleMessage(const QString &type, const QString &text)
{
    // "traduku: " in front, because these lines arrive interleaved with the
    // report and a bare sentence cannot be attributed to a program.
    if (text.isEmpty()) {
        return {};
    }
    // Twice-prefixed lines are what an unexamined prefix produces: the advice
    // strings already carry "traduku: " (they are also printed directly), and
    // "traduku: traduku: ..." reads like two programs arguing.
    if (text.startsWith(QLatin1String("traduku:"))) {
        return text;
    }
    static const QStringList severe = {QStringLiteral("warning"),
                                       QStringLiteral("critical"),
                                       QStringLiteral("fatal")};
    if (severe.contains(type, Qt::CaseInsensitive)) {
        return QStringLiteral("traduku: %1").arg(text);
    }
    return text;
}

ConsoleDecision decideConsoleAttach(bool onWindows, bool hasConsole,
                                   bool trayApp)
{
    // Only ever on Windows: attaching to a parent console is a Win32 call, and
    // on every other OS the process already has the terminal it was started
    // from.
    //
    // Only for the modes that were asked to print something: the tray app has
    // no parent console to attach to, and "attach to whoever launched me" is a
    // bug waiting for a shell that happens to have one - its banner belongs in
    // the log. Note --selftest is NOT the tray app (printsToTerminal).
    if (!onWindows || hasConsole || trayApp) {
        return ConsoleDecision::LeaveAsIs;
    }
    return ConsoleDecision::Attach;
}

QString desktopBaseName()
{
    // Deliberately not derived from QCoreApplication::applicationName(): that
    // one is set by configureApplication and a test could have changed it,
    // while this names a file on disk.
    return QStringLiteral("traduku");
}

int runAutostartCommand(const QString &action)
{
    QTextStream out(stdout), err(stderr);
    // The same object the tray menu drives - one implementation of a promise,
    // reached two ways. A second copy here would eventually disagree with the
    // menu, and the user would have no way to tell which one lied. It is also
    // the object of *this platform*: a Windows `--autostart=on` that wrote an
    // XDG .desktop file would answer with a path, a success code and a lie.
    std::unique_ptr<IAutostart> autostartHolder =
        createDefaultAutostart(QCoreApplication::applicationFilePath());
    IAutostart &autostart = *autostartHolder;
    if (action == QLatin1String("status")) {
        out << "autostart: " << autostart.diagnostic() << '\n';
        return 0;                       // a question, not an assertion
    }
    const bool on = (action == QLatin1String("on"));
    if (!autostart.setEnabled(on)) {
        // backendName(), not a file path: on Windows the thing that failed is a
        // registry key, and an error message naming a file that does not exist
        // is how a support question starts.
        err << "traduku: could not " << (on ? "enable" : "disable")
            << " autostart in " << autostart.backendName() << '\n';
        return 1;
    }
    out << "autostart: " << autostart.diagnostic() << '\n';
    return 0;
}

QString readyLine(const ReadyInfo &info)
{
    // One line, key=value: the harness greps it (tests/e2e/grid.py
    // wait_journal), and a line that wrapped would make the suite fall back to
    // sleeping - invisibly, because a wait that never matches just times out.
    // Every field names a backend the app chose, so the same line answers
    // "which clipboard did it pick this time?" without --backend-info.
    const QString hotkey = info.hotkey.isEmpty() ? QStringLiteral("none") : info.hotkey;
    return QStringLiteral("traduku: ready (%1, %2; clipboard=%3 hotkey=%4 tray=%5)")
        .arg(info.shell.isEmpty() ? QStringLiteral("unknown") : info.shell,
             info.session.isEmpty() ? QStringLiteral("unknown") : info.session,
             info.clipboard.isEmpty() ? QStringLiteral("none") : info.clipboard,
             hotkey,
             info.trayAvailable ? QStringLiteral("yes") : QStringLiteral("no"));
}

void logReady(const ReadyInfo &info)
{
    qInfo("%s", qUtf8Printable(readyLine(info)));
}

SingleInstanceVerdict singleInstanceVerdict(const QString &errorText)
{
    // One decision, both halves of it: the sentence AND whether that is a
    // success. A duplicate is not a failure (the user asked for a running
    // Traduku and has one); everything else means the app never came up and
    // has to leave a non-zero status, or a script that starts traduku keeps
    // believing it worked.
    SingleInstanceVerdict verdict;
    if (errorText.contains(QStringLiteral("already owned"))) {
        verdict.duplicate = true;
        verdict.exitCode = 0;
        verdict.message = QStringLiteral("Traduku is already running (asked it to "
                                        "surface its window).\n");
        return verdict;
    }
    verdict.exitCode = 1;
    verdict.message = QStringLiteral("Traduku did not start: %1\n")
        .arg(errorText.isEmpty() ? QStringLiteral("single-instance check failed")
                                : errorText);
    return verdict;
}

} // namespace lt
