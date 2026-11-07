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

class TestAppFlavour : public QObject
{
    Q_OBJECT
private slots:
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
