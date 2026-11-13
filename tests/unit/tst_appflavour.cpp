/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "app/AppFlavour.h"

#include "core/settings.h"
#include "core/soundplayer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

using lt::qpaWasForcedByUs;
using lt::decideQpa;
using lt::QpaDecision;
using lt::qtCanPickAPlatform;
using lt::needsWidgets;
using lt::prepareCliEnvironment;
using lt::planFirstRun;
using lt::FirstRunPlan;
using lt::guiRefusalNeedsDialog;
using lt::trayStatusLabel;
using lt::soundReport;
using lt::SoundReport;
using lt::Settings;
using lt::SoundPlayer;

class TestAppFlavour : public QObject
{
    Q_OBJECT
private slots:
    /// A refusal nobody can read is not a refusal.
    ///
    /// The GUI mode used to print "there is no display" to stderr and exit 2
    /// when double-clicked from Explorer - a terminal that does not exist. The
    /// desktop bug itself is fixed (the question is now asked of the OS);
    /// these rows keep the *symptom* from coming back as silence, because
    /// silence is what made it survive.
    void aRefusalWithoutATerminalGoesOnTheScreen()
    {
        QVERIFY(guiRefusalNeedsDialog(/*consoleAvailable=*/false));
        // And with a terminal it stays text: a modal in front of someone
        // reading a console, or in front of a CI runner, is a worse bug than
        // the one being fixed here.
        QVERIFY(!guiRefusalNeedsDialog(/*consoleAvailable=*/true));
    }

    /// The tray's status line, spelled in characters rather than in bytes.
    ///
    /// It read `QStringLiteral("Traduku %1 \xC2\xB7 tray %2")` - UTF-8 bytes
    /// pasted into a UTF-16 literal. QStringLiteral makes a QChar per *byte*, so
    /// the separator arrived as U+00C2 U+00B7 and the tray menu showed "Traduku
    /// 0.3.0 Â· tray qt": a mojibake the user reported from the live desktop,
    /// and the kind of thing no amount of reading the source catches, because
    /// the source looks correct.
    void theTrayStatusLineIsTextNotBytes()
    {
        const QString line = trayStatusLabel(QStringLiteral("0.3.0"),
                                             QStringLiteral("qt"));
        QVERIFY2(!line.contains(QChar(0x00C2)),    // the "Â" of a double-encoded UTF-8
                 qPrintable(QStringLiteral("mojibake in the tray line: [") + line
                            + QStringLiteral("]")));
        QVERIFY2(line.contains(QStringLiteral("0.3.0")),
                 qPrintable(QStringLiteral("no version in: [") + line + QStringLiteral("]")));
        QVERIFY2(line.contains(QStringLiteral("tray qt")),
                 qPrintable(QStringLiteral("no tray kind in: [") + line
                            + QStringLiteral("]")));
        // The separator is one character, not a two-byte imitation of one.
        QVERIFY2(line.contains(QChar(0x00B7)) || line.contains(QLatin1Char('-')),
                 qPrintable(QStringLiteral("no separator in: [") + line
                            + QStringLiteral("]")));
        QVERIFY2(line.size() < QStringLiteral("Traduku 0.3.0 ").size() + 32,
                 qPrintable(QStringLiteral("the line grew past its parts: [")
                            + line + QStringLiteral("]")));
    }

    // First run is where a tray-less desktop is won or lost: the app is
    // installed, there is no icon (GNOME shows one only with the appindicator
    // extension), and nobody has told the user that Ctrl+C twice is the whole
    // interface. The shells must not share this decision blindly - Plasma asks
    // about autostart because nothing else does, GNOME must not ask because
    // the package already installed the entry.
    void readyLineNamesWhatTheHarnessWaitsFor()
    {
        // The E2E harness used to wait `sleep 4` for "the app is probably up
        // now": 4 s per test x 22 tests of pure waiting, and still flaky when
        // a VM was loaded. The product already knows when it is ready, so it
        // says so, and the harness waits for the sentence instead of a guess.
        lt::ReadyInfo i;
        i.shell = QStringLiteral("gnome");
        i.session = QStringLiteral("wayland");
        i.clipboard = QStringLiteral("x11");
        i.hotkey = QStringLiteral("x11-record");
        i.trayAvailable = true;
        const QString line = lt::readyLine(i);
        QVERIFY2(line.startsWith(QStringLiteral("traduku: ready")),
                 qPrintable(QStringLiteral("got: ") + line));
        QVERIFY(line.contains(QStringLiteral("gnome")));
        QVERIFY(line.contains(QStringLiteral("wayland")));
        QVERIFY(line.contains(QStringLiteral("clipboard=x11")));
        QVERIFY(line.contains(QStringLiteral("hotkey=x11-record")));
        QVERIFY(line.contains(QStringLiteral("tray=yes")));
        // One line. The harness greps a single journal line; a wrapped or
        // multi-line "ready" is invisible to it and the suite goes back to
        // sleeping - quietly, because the wait would just time out.
        QVERIFY2(!line.contains(QLatin1Char('\n')) && !line.contains(QLatin1Char('\r')),
                 qPrintable(QStringLiteral("readyLine must be one line, got: ") + line));

        // Tray-less is the normal GNOME case (no appindicator extension) and
        // still has to be reported, not omitted: "no tray" and "the app is
        // hung before the tray" have to stay distinguishable in the journal.
        i.trayAvailable = false;
        i.hotkey = QString();
        const QString bare = lt::readyLine(i);
        QVERIFY(bare.contains(QStringLiteral("tray=no")));
        QVERIFY(bare.contains(QStringLiteral("hotkey=none")));
    }

    void firstRunGuidanceDiffersByShellWithoutDivergingInCode()
    {
        const FirstRunPlan gnomeNoTray = planFirstRun(QStringLiteral("gnome"), false);
        QVERIFY(gnomeNoTray.greet);
        QVERIFY(!gnomeNoTray.askAboutAutostart);
        QVERIFY(gnomeNoTray.explainMissingIcon);

        // With an SNI host the icon exists; extension advice would then be the
        // same noise as telling someone to install what they installed.
        const FirstRunPlan gnomeWithTray = planFirstRun(QStringLiteral("gnome"), true);
        QVERIFY(gnomeWithTray.greet);
        QVERIFY(!gnomeWithTray.explainMissingIcon);
        QVERIFY(!gnomeWithTray.askAboutAutostart);

        const FirstRunPlan plasma = planFirstRun(QStringLiteral("kde"), true);
        QVERIFY(plasma.greet);
        QVERIFY(plasma.askAboutAutostart);
        QVERIFY(!plasma.explainMissingIcon);

        // Whether to put the settings dialog in front of someone depends on
        // whether there is anything to configure: an app autostarted at login
        // that demands its API endpoint is the app not starting, and an app
        // that demands it when the endpoint is already set is a nag.
        QVERIFY(!planFirstRun(QStringLiteral("gnome"), true, true).openSettings);
        QVERIFY(planFirstRun(QStringLiteral("gnome"), true, false).openSettings);
        QVERIFY(planFirstRun(QStringLiteral("kde"), true, true).openSettings);
    }

    void onlyTheModesThatDrawNeedWidgets()
    {
        // The tray app is the default (no mode flag) and must keep wanting a
        // real QPA: a traduku that "runs" with no desktop is a missing icon
        // the user cannot explain.
        QVERIFY(needsWidgets({QStringLiteral("traduku")}));
        QVERIFY(needsWidgets({QStringLiteral("traduku"), QStringLiteral("--interval"),
                             QStringLiteral("5s")}));
        // --selftest screenshots itself: widgets, and it is run under an
        // explicit offscreen QPA by the gate, not by us.
        QVERIFY(needsWidgets({QStringLiteral("traduku"), QStringLiteral("--selftest")}));

        QVERIFY(!needsWidgets({QStringLiteral("traduku"), QStringLiteral("--backend-info")}));
        QVERIFY(!needsWidgets({QStringLiteral("traduku"), QStringLiteral("--translate")}));
        QVERIFY(!needsWidgets({QStringLiteral("traduku"), QStringLiteral("--text"),
                               QStringLiteral("hi")}));
        QVERIFY(!needsWidgets({QStringLiteral("traduku"), QStringLiteral("--notify-test")}));
        // --autostart writes one file and prints a line. Without this entry it
        // is treated as "no mode flag" -> QApplication -> qFatal on a machine
        // with no display, which is where the user without a panel icon is.
        QVERIFY(!needsWidgets({QStringLiteral("traduku"), QStringLiteral("--autostart"),
                               QStringLiteral("status")}));
        // Same option in its --flag=value form, which is what a user types and
        // what the e2e runs. Comparing whole tokens made this form count as
        // "no mode flag" -> QApplication -> abort on a machine with no
        // display, and the unit test above passed anyway because it happened
        // to use the two-token spelling.
        QVERIFY(!needsWidgets({QStringLiteral("traduku"),
                               QStringLiteral("--autostart=status")}));
        QVERIFY(!needsWidgets({QStringLiteral("traduku"),
                               QStringLiteral("--autostart=off")}));
        QVERIFY(!needsWidgets({QStringLiteral("traduku"),
                               QStringLiteral("--text=hello"),
                               QStringLiteral("--translate")}));
        QVERIFY(!needsWidgets({QStringLiteral("traduku"), QStringLiteral("--help")}));
        QVERIFY(!needsWidgets({QStringLiteral("traduku"), QStringLiteral("--version")}));
    }

    // A mode that needs a real display, on a machine that cannot give Qt one,
    // must SAY so and exit - not abort. Measured over plain ssh on arch-kde:
    // `traduku --selftest` produced "Aborted (core dumped)", rc 134, and no
    // sentence to read, because QApplication reached Qt's createPlatformIntegration
    // fatal. The gate passes an explicit offscreen QPA and is unaffected; this is
    // about the human who types the command in a support thread and gets a core
    // file instead of an instruction.
    void aModeThatNeedsADisplaySaysSoInsteadOfCrashing()
    {
        // The suite runs under QT_QPA_PLATFORM=offscreen (ctest sets it), and this
        // function deliberately reads that variable - a user who set one has
        // answered the question. So the cases that expect advice have to say what
        // the machine they model actually looks like.
        QProcessEnvironmentGuard g;
        qunsetenv("QT_QPA_PLATFORM");
        qunsetenv("DISPLAY");
        qunsetenv("WAYLAND_DISPLAY");
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--selftest")}, false, false)
                .contains(QStringLiteral("QT_QPA_PLATFORM")));
        QVERIFY(!lt::displayAdviceForMissingDisplay(
                     {QStringLiteral("traduku")}, false, false).isEmpty());
        // The console modes are rescued by decideQpa() and must not be refused:
        // they are the commands a user runs precisely when the desktop is broken.
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--backend-info")}, false, false).isEmpty());
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--autostart=off")}, false, false).isEmpty());
        // With a display - or with a QPA of the user's own - there is nothing to
        // advise, and the modes run as they always did.
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--selftest")}, true, false).isEmpty());
        qputenv("QT_QPA_PLATFORM", "offscreen");   // the user answered already
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--selftest")}, false, false).isEmpty());
    }

    // The same refusal on Windows must not send the user to export DISPLAY.
    void theMissingDisplayAdviceNamesTheOperatingSystemItIsSpeakingOn()
    {
        QProcessEnvironmentGuard g;
        qunsetenv("QT_QPA_PLATFORM");
        const QStringList selftest = {QStringLiteral("traduku"),
                                      QStringLiteral("--selftest")};
        const QString win = lt::displayAdviceForMissingDisplay(selftest, false, true);
        QVERIFY(!win.isEmpty());
        QVERIFY2(win.contains(QStringLiteral("desktop session")), qPrintable(win));
        QVERIFY2(!win.contains(QStringLiteral("WAYLAND_DISPLAY")),
                 "a Windows refusal that tells the user to export DISPLAY is "
                 "advice nobody can follow");
        // Named unixAdvice, not unix: GNU C defines `unix` (and `linux`) as
        // macro =1 in standard-macros mode, so `const QString unix` expands to
        // `const QString 1` - it compiles under MinGW, which does not define
        // the macro, and breaks the build on every Linux compiler. This line
        // is why: the Windows port landed green on its own box and red on
        // Debian, where nobody ran the gate afterwards.
        const QString unixAdvice = lt::displayAdviceForMissingDisplay(selftest, false, false);
        QVERIFY2(unixAdvice.contains(QStringLiteral("DISPLAY")), qPrintable(unixAdvice));
    }

    void aHumanChosenQpaIsNeverOverwritten()
    {
        QProcessEnvironmentGuard g;
        qunsetenv("DISPLAY");
        qunsetenv("WAYLAND_DISPLAY");
        qputenv("QT_QPA_PLATFORM", "xcb");   // the user's explicit choice
        prepareCliEnvironment({QStringLiteral("traduku"), QStringLiteral("--backend-info")});
        QCOMPARE(QByteArray(qgetenv("QT_QPA_PLATFORM")), QByteArray("xcb"));
    }

    void qtOnlyTrustsWhatIsInTheEnvironment()
    {
        // The distinction that made this a bug: over ssh the session's
        // XWayland is still reachable (via /proc), so "is X reachable" is
        // true - while Qt, which reads only DISPLAY/WAYLAND_DISPLAY, cannot
        // start at all. Answering the first question for both left
        // --backend-info aborting.
        QProcessEnvironmentGuard g;
        qunsetenv("DISPLAY");
        qunsetenv("WAYLAND_DISPLAY");
        QVERIFY(!qtCanPickAPlatform());
        qputenv("DISPLAY", ":0");
        QVERIFY(qtCanPickAPlatform());
        qunsetenv("DISPLAY");
        qputenv("WAYLAND_DISPLAY", "wayland-0");
        QVERIFY(qtCanPickAPlatform());
    }

    void theQpaDecisionIsATableNotAnEnvironment()
    {
        // Rows, not environment mutation: a process-level "did we force it"
        // flag asked about after another call already forced it is how the
        // first version of this test failed on itself rather than on the code.
        const QStringList cli{QStringLiteral("traduku"), QStringLiteral("--backend-info")};
        const QStringList gui{QStringLiteral("traduku")};

        QCOMPARE(decideQpa(cli, false, false), QpaDecision::ForceOffscreen);
        QCOMPARE(decideQpa(cli, true, false), QpaDecision::LeaveAsIs);   // human chose
        QCOMPARE(decideQpa(cli, false, true), QpaDecision::LeaveAsIs);   // Qt can pick
        QCOMPARE(decideQpa(gui, false, false), QpaDecision::LeaveAsIs);  // must fail loudly
        QCOMPARE(decideQpa(gui, false, true), QpaDecision::LeaveAsIs);
    }

    void consoleModesGetAPlatformOnAHeadlessBox()
    {
        // The regression itself, end to end: no DISPLAY, no WAYLAND_DISPLAY,
        // no QT_QPA_PLATFORM - the ssh environment a user types --backend-info
        // in. Before this existed the process aborted with zero bytes out.
        QProcessEnvironmentGuard g;
        qunsetenv("DISPLAY");
        qunsetenv("WAYLAND_DISPLAY");
        qunsetenv("QT_QPA_PLATFORM");
        prepareCliEnvironment({QStringLiteral("traduku"), QStringLiteral("--backend-info")});
        QCOMPARE(QByteArray(qgetenv("QT_QPA_PLATFORM")), QByteArray("offscreen"));
        // The report says "(no display here)" only when it was our doing, and
        // this is the case where it was.
        QVERIFY(qpaWasForcedByUs());
    }

    // ---- Windows: the console a GUI-subsystem binary does not have -------
    //
    // `traduku --translate` from cmd on Windows is a GUI executable with no
    // console of its own: without attaching to the parent's, the streamed
    // translation and every error go nowhere and only the exit code comes
    // back. The tray app must NOT do it - it has no parent console, and
    // "attach to whoever launched me" from Explorer is a bug waiting for a
    // shell that has one.
    void consoleAttachOnlyForConsoleModesOnWindows()
    {
        using lt::ConsoleDecision;
        QCOMPARE(lt::decideConsoleAttach(true, false, false), ConsoleDecision::Attach);
        QCOMPARE(lt::decideConsoleAttach(true, true, false), ConsoleDecision::LeaveAsIs);
        QCOMPARE(lt::decideConsoleAttach(true, false, true), ConsoleDecision::LeaveAsIs);
        // Not Windows: the process already has the terminal it was started
        // from, and attaching to a parent is nonsense there.
        QCOMPARE(lt::decideConsoleAttach(false, false, false), ConsoleDecision::LeaveAsIs);
        QCOMPARE(lt::decideConsoleAttach(false, false, true), ConsoleDecision::LeaveAsIs);
    }

    // --selftest needs widgets *and* prints, so it must not be classified as
    // the tray app: on Windows that classification is the difference between a
    // report and a silent exit code.
    // A GUI binary has no console, and Qt's default handler on Windows notices
    // and sends qWarning() to the debugger. These rows pin the replacement:
    // every diagnostic that matters reaches the terminal, and says who said it.
    void qtDiagnosticsBecomeAttributableTerminalLines()
    {
        QCOMPARE(lt::qtConsoleMessage(QStringLiteral("warning"),
                                       QStringLiteral("no display")),
                 QStringLiteral("traduku: no display"));
        QCOMPARE(lt::qtConsoleMessage(QStringLiteral("fatal"),
                                       QStringLiteral("boom")),
                 QStringLiteral("traduku: boom"));
        // Debug chatter keeps its text but does not get the product prefix:
        // it is not a message to the user, and dressing it as one makes the
        // report unreadable.
        QCOMPARE(lt::qtConsoleMessage(QStringLiteral("debug"),
                                      QStringLiteral("hook installed")),
                 QStringLiteral("hook installed"));
        // An empty diagnostic must produce no line at all, or the handler
        // prints blank rows between the selftest's PASS lines.
        QVERIFY(lt::qtConsoleMessage(QStringLiteral("warning"), QString()).isEmpty());
        // Advice strings are also printed directly and already carry the
        // prefix; adding it again gives "traduku: traduku: ...".
        QCOMPARE(lt::qtConsoleMessage(
                     QStringLiteral("warning"),
                     QStringLiteral("traduku: no display here")),
                 QStringLiteral("traduku: no display here"));
    }

    // A QPA the human named that is not installed is their mistake, but the
    // app is the one that dies: Qt reaches qFatal/QMessageLogger and the GUI
    // binary ends in a dialog that waits for a click (measured - a stray
    // traduku.exe behind an invisible window, killed by hand). Refusing in
    // words, before Qt is asked, is the product's job.
    void aNamedButAbsentPlatformPluginIsRefusedInWords()
    {
        const QStringList installed = {QStringLiteral("windows"),
                                       QStringLiteral("offscreen")};
        // Nothing named: the platform picks its own default, not our business.
        QVERIFY(lt::platformPluginProblem(QString(), installed).isEmpty());
        // Named and present (including the "name:option" spelling Qt accepts).
        QVERIFY(lt::platformPluginProblem(QStringLiteral("offscreen"), installed)
                    .isEmpty());
        QVERIFY(lt::platformPluginProblem(QStringLiteral("windows:darkmode=1"),
                                          installed).isEmpty());
        QVERIFY(lt::platformPluginProblem(QStringLiteral(" WINDOWS "), installed)
                    .isEmpty());

        const QString problem = lt::platformPluginProblem(
            QStringLiteral("traduku-no-such-platform"), installed);
        QVERIFY2(!problem.isEmpty(), "the typo was accepted");
        QVERIFY2(problem.contains(QStringLiteral("traduku-no-such-platform")),
                 qPrintable(QStringLiteral("the refused name is not echoed: ")
                            + problem));
        // ... and the fix has to be visible from here: an empty list would
        // leave the reader guessing what this machine does have.
        QVERIFY2(problem.contains(QStringLiteral("windows"))
                     && problem.contains(QStringLiteral("offscreen")),
                 qPrintable(QStringLiteral("the installed platforms are not "
                                           "listed: ") + problem));
        // A machine with no plugins at all is a different sentence: naming an
        // empty choice list reads like a bug in the report.
        const QString none = lt::platformPluginProblem(QStringLiteral("offscreen"), {});
        QVERIFY2(!none.isEmpty(), "an empty install was accepted");
        QVERIFY2(none.contains(QStringLiteral("no platform plugins")),
                 qPrintable(none));
    }

    void selftestIsATerminalReportNotTheTrayApp()
    {
        using lt::ConsoleDecision;
        QVERIFY(lt::printsToTerminal({QStringLiteral("traduku"),
                                      QStringLiteral("--selftest")}));
        QVERIFY(lt::printsToTerminal({QStringLiteral("traduku"),
                                      QStringLiteral("--selftest"),
                                      QStringLiteral("--live")}));
        QVERIFY(lt::printsToTerminal({QStringLiteral("traduku"),
                                      QStringLiteral("--backend-info")}));
        QVERIFY(!lt::printsToTerminal({QStringLiteral("traduku")}));
        QVERIFY(lt::needsWidgets({QStringLiteral("traduku"),
                                  QStringLiteral("--selftest")}));
        // ... and the two rules still disagree in exactly the way they should.
        QCOMPARE(lt::decideConsoleAttach(true, false,
                                         !lt::printsToTerminal({QStringLiteral("traduku"),
                                                                QStringLiteral("--selftest")})),
                 ConsoleDecision::Attach);
        QCOMPARE(lt::decideConsoleAttach(true, false,
                                         !lt::printsToTerminal({QStringLiteral("traduku")})),
                 ConsoleDecision::LeaveAsIs);
    }

    // ---- the sound report names every cue, on every shell --------------
    //
    // Three copies of this block lived in the three main()s, each printing the
    // status of SoundEvent::Finished alone. The Windows hand walk (ROADMAP.md
    // \u00a77.8 item 2) then read "the profile has three cues, the report names
    // one" - and a report that names one cue cannot tell you which of the other
    // two is the silent one. Same rule that moved the tray line here: one
    // portable function, tested on every platform, no main() able to get it
    // wrong again.
    void everyCueIsNamedInTheSoundReport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const char *n : {"bell.oga", "complete.oga", "dialog-error.oga"}) {
            QFile f(QDir(dir.path()).filePath(QLatin1String(n)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
            f.close();
        }
        Settings s(QDir(dir.path()).filePath(QStringLiteral("config.ini")));
        SoundPlayer player(&s, {}, {dir.path()}, false,
                           [](const QString &prog) { return prog; });

        const SoundReport r = soundReport(player);
        QVERIFY2(r.summary.contains(QStringLiteral("freedesktop")),
                 qPrintable(QStringLiteral("the report does not name the profile: ")
                            + r.summary));
        QVERIFY2(r.cues.size() == 3,
                 qPrintable(QStringLiteral("a profile with three cues, a report with ")
                            + QString::number(r.cues.size())));
        const QStringList events{QStringLiteral("started"), QStringLiteral("finished"),
                                 QStringLiteral("failed")};
        for (int i = 0; i < r.cues.size(); ++i) {
            QVERIFY2(r.cues.at(i).startsWith(events.at(i) + QLatin1Char(':')),
                     qPrintable(QStringLiteral("cue ") + QString::number(i)
                                + QStringLiteral(" is not the event it should be: ")
                                + r.cues.at(i)));
            // Resolved: the line carries a path, and that file exists.
            const QString path = r.cues.at(i).section(QLatin1Char(':'), 1).trimmed();
            QVERIFY2(QFile::exists(path),
                     qPrintable(QStringLiteral("a clean cue names a file that is "
                                               "not there: ") + path));
        }
    }

    /// A cue that cannot resolve has to say why, per cue: "no sound" from one
    /// user is "nothing is wrong" from the report when the report only looked
    /// at the event that happens to work.
    void anUnresolvableCueIsReportedWithItsProblem()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(QDir(dir.path()).filePath(QStringLiteral("config.ini")));
        // No files at all; every player "installed", so the only complaint
        // left is the missing theme - and it must be attached to each cue.
        SoundPlayer player(&s, {}, {dir.path()}, false,
                           [](const QString &prog) { return prog; });
        const SoundReport r = soundReport(player);
        QCOMPARE(r.cues.size(), 3);
        for (const QString &line : r.cues) {
            // A cue that resolves to nothing must not report a file: the part
            // after the colon is the advice, and the advice has to be readable
            // as advice.
            QVERIFY2(line.contains(QStringLiteral("no sound theme")),
                     qPrintable(QStringLiteral("the problem is not the sentence "
                                               "the profile wrote: ") + line));
        }
    }

    /// The user's own switch is one sentence, not three complaints: advice
    /// about a feature somebody turned off is what teaches a user to skip the
    /// advice that is true.
    void aSwitchedOffSoundIsOneLineAndNoCueComplaints()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(QDir(dir.path()).filePath(QStringLiteral("config.ini")));
        s.setSoundsEnabled(false);
        SoundPlayer player(&s, {}, {dir.path()}, false,
                           [](const QString &prog) { return prog; });
        const SoundReport r = soundReport(player);
        QVERIFY2(r.summary.startsWith(QStringLiteral("off")), qPrintable(r.summary));
        QVERIFY2(r.cues.isEmpty(),
                 "a switched-off sound produced per-cue problems to read");
    }

    // ---- Windows: what the first launch asks ----------------------------
    //
    // Windows ships no package and installs no autostart entry, so the app
    // must ASK (the Plasma rule), and it must not be told the tray is missing
    // unless the tray really is: on this OS a shell is always there.
    void windowsFirstRunAsksAboutAutostart()
    {
        const FirstRunPlan win = lt::planFirstRun(QStringLiteral("windows"), true, false);
        QVERIFY(win.greet);
        QVERIFY(win.askAboutAutostart);
        QVERIFY(win.openSettings);        // nothing to translate with yet
        QVERIFY(!win.explainMissingIcon); // tray available -> nothing to explain

        const FirstRunPlan winTrayless = lt::planFirstRun(QStringLiteral("windows"),
                                                         false, true);
        QVERIFY(winTrayless.explainMissingIcon);
        QVERIFY(!winTrayless.openSettings);   // endpoint set: do not block them
    }

private:
    /// Restores QT_QPA_PLATFORM exactly as the harness set it. The suite runs
    /// under offscreen from CTest; leaking a sentinel into the next test in
    /// this binary would be a test bug masquerading as a product bug.
    struct QProcessEnvironmentGuard
    {
        QByteArray saved = qgetenv("QT_QPA_PLATFORM");
        ~QProcessEnvironmentGuard() { qputenv("QT_QPA_PLATFORM", saved); }
    };
};

QTEST_GUILESS_MAIN(TestAppFlavour)
#include "tst_appflavour.moc"
