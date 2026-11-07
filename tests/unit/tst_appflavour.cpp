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
