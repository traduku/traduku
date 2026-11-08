/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

// The double-click test. Everything else in this suite asks the program a
// question it can answer from a service session; these rows ask whether a
// person who double-clicks traduku.exe gets a program, which needs either a
// desktop or the honest absence of one - and the two cases have to be
// measured separately, because a suite that only ever runs in the second
// teaches everyone that the first one works.
//
// What this suite was written for: the GUI mode asked `qtCanPickAPlatform()`,
// which reads DISPLAY and WAYLAND_DISPLAY. On Windows those are never set, so
// the answer was "there is no display" on every Windows desktop in existence,
// the app printed its advice and exited 2 - and from Explorer there is no
// terminal to read the advice in, so the whole product was a double-click that
// did nothing. The fact needed was already being collected two directories
// away (WinSessionFacts::interactiveDesktop); the caller had simply not asked
// it.

#include <QtTest>

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSysInfo>

#include "platform/win/WinSessionInfo.h"

#ifndef TRADUKU_EXE
#error "TRADUKU_EXE must name the built traduku.exe"
#endif

class TestWinGui : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_exe = QString::fromUtf8(TRADUKU_EXE);
        QVERIFY2(QFileInfo::exists(m_exe), qPrintable(m_exe + QStringLiteral(" missing")));
        // Asked once, of the OS: which of the two mutually exclusive rows below
        // can actually run here. Both can never run on the same machine, which
        // is exactly why the pair is written as a skip rather than as one
        // assertion that would be wrong half the time.
        m_haveDesktop = lt::queryWinSessionFacts().interactiveDesktop;
    }

    /// No interactive desktop: the refusal is what has to happen, and it has to
    /// be *said*. An X11 environment variable is not a Windows desktop - the
    /// bug was answering this question with `DISPLAY`.
    void refusalNamesTheDesktopWhenThereIsNone()
    {
        if (m_haveDesktop) {
            QSKIP("this session has an interactive desktop, so the refusal "
                  "cannot be measured here - guiModeRunsOnADesktopSession is "
                  "the row for that machine");
        }
        // DISPLAY and WAYLAND_DISPLAY are planted on purpose: the old code
        // treated them as proof of a display, so with them set the app stopped
        // refusing and died inside Qt instead - either way, a person who
        // double-clicked got nothing, and the report said so in Unix terms.
        QProcess p;
        p.setProcessEnvironment(envWith({{QStringLiteral("DISPLAY"),
                                          QStringLiteral(":0")},
                                         {QStringLiteral("WAYLAND_DISPLAY"),
                                          QStringLiteral("wayland-0")}}));
        p.start(m_exe, {});                      // no arguments: the GUI mode
        QVERIFY2(p.waitForStarted(10000), qPrintable(p.errorString()));
        QVERIFY2(p.waitForFinished(20000),
                 "the GUI mode neither started nor refused: it hung");
        const QString err = QString::fromLocal8Bit(p.readAllStandardError());
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p.exitCode(), 2);
        QVERIFY2((out + err).contains(QStringLiteral("desktop")),
                 qPrintable(QStringLiteral("the refusal must name the desktop "
                                           "(not DISPLAY): [") + out + err
                            + QStringLiteral("]")));
        QVERIFY2(!(out + err).contains(QStringLiteral("DISPLAY")),
                 qPrintable(QStringLiteral("a Windows user was told about an "
                                           "X11 variable: [") + out + err
                            + QStringLiteral("]")));
    }

    /// An interactive desktop: no refusal at all - the program starts and stays
    /// started. Checked as "still alive after a few seconds, and nothing was
    /// said about a missing display", because waiting for the tray icon itself
    /// is not something a headless suite can see; this row is the one that
    /// turns green on the user's machine and is skipped on a build box.
    void guiModeRunsOnADesktopSession()
    {
        if (!m_haveDesktop) {
            QSKIP("no interactive desktop in this session (a service session "
                  "has no window station) - this row can only be measured on a "
                  "machine with a logged-in desktop");
        }
        QProcess p;
        // Deliberately NOT QT_QPA_PLATFORM=offscreen: the point is the real
        // platform, the real window station and the real tray.
        p.setProcessEnvironment(envWith({}));
        p.start(m_exe, {});
        QVERIFY2(p.waitForStarted(10000), qPrintable(p.errorString()));
        if (!p.waitForFinished(4000)) {
            // The answer this row wants: the program is up and staying up.
            // Left running, it would hold a keyboard hook for the rest of the
            // suite, so it is asked to leave the same way the tray menu does.
            p.terminate();
            QVERIFY2(p.waitForFinished(10000),
                     "the GUI mode would not quit when asked - a tray app that "
                     "ignores termination is its own bug");
            return;
        }
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        const QString err = QString::fromLocal8Bit(p.readAllStandardError());
        QVERIFY2(!(out + err).contains(QStringLiteral("no interactive desktop")),
                 qPrintable(QStringLiteral("the GUI mode refused a session that "
                                           "has a desktop: [") + out + err
                            + QStringLiteral("]")));
        QFAIL("the GUI mode exited without refusing and without a message: a "
              "double-click that does nothing is what this row exists to catch");
    }

    /// The console modes must still work in the same session: the GUI fix may
    /// not turn --translate into a program that needs a window station.
    void consoleModesStillWorkWithoutADesktop()
    {
        QProcess p;
        p.setProcessEnvironment(envWith({{QStringLiteral("QT_QPA_PLATFORM"),
                                          QStringLiteral("offscreen")}}));
        p.start(m_exe, {QStringLiteral("--version")});
        QVERIFY2(p.waitForStarted(10000), qPrintable(p.errorString()));
        QVERIFY2(p.waitForFinished(20000), qPrintable(p.errorString()));
        QCOMPARE(p.exitCode(), 0);
        QVERIFY(QString::fromLocal8Bit(p.readAllStandardOutput())
                    .contains(QStringLiteral("traduku")));
    }

private:
    QProcessEnvironment envWith(const QList<QPair<QString, QString>> &extra)
    {
        // A copy of the test's own environment, which on Windows means WINDIR,
        // SYSTEMROOT and USERPROFILE are present as they would be for a program
        // started from Explorer - the failure modes of a stripped environment
        // are a different suite's problem (tst_consolefatal covers that side).
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        // remove(), not insert(""): an entry present-but-empty still counts as
        // "a human named a platform", which is a different branch of the QPA
        // decision and would have tested the wrong one.
        env.remove(QStringLiteral("QT_QPA_PLATFORM"));
        for (const auto &e : extra) {
            env.insert(e.first, e.second);
        }
        return env;
    }

    QString m_exe;
    bool m_haveDesktop = false;
};

QTEST_GUILESS_MAIN(TestWinGui)
#include "tst_wingui.moc"
