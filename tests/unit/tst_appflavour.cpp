/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "app/AppFlavour.h"

#include <QCoreApplication>
#include <QObject>
#include <QtTest>

using lt::qpaWasForcedByUs;
using lt::decideQpa;
using lt::QpaDecision;
using lt::qtCanPickAPlatform;
using lt::needsWidgets;
using lt::prepareCliEnvironment;
using lt::planFirstRun;
using lt::FirstRunPlan;

class TestAppFlavour : public QObject
{
    Q_OBJECT
private slots:
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
                    {QStringLiteral("traduku"), QStringLiteral("--selftest")}, false)
                .contains(QStringLiteral("QT_QPA_PLATFORM")));
        QVERIFY(!lt::displayAdviceForMissingDisplay(
                     {QStringLiteral("traduku")}, false).isEmpty());
        // The console modes are rescued by decideQpa() and must not be refused:
        // they are the commands a user runs precisely when the desktop is broken.
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--backend-info")}, false).isEmpty());
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--autostart=off")}, false).isEmpty());
        // With a display - or with a QPA of the user's own - there is nothing to
        // advise, and the modes run as they always did.
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--selftest")}, true).isEmpty());
        qputenv("QT_QPA_PLATFORM", "offscreen");   // the user answered already
        QVERIFY(lt::displayAdviceForMissingDisplay(
                    {QStringLiteral("traduku"), QStringLiteral("--selftest")}, false).isEmpty());
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
