/*
    A console mode must explain itself and leave; it may not open a dialog and
    wait for a hand that is not there.

    Measured, not imagined: the portable package initially shipped without the
    offscreen platform plugin, and running `traduku.exe --selftest` with
    QT_QPA_PLATFORM=offscreen produced a Qt "this application failed to start"
    *message box*. From a terminal that is a process that never returns; from a
    scheduled task or an ssh session it is a process that lives forever behind
    a window nobody can see (one was left running here until it was killed by
    hand). The same run through the console handler must print a line and exit
    with the code the CLI already uses for "the machine cannot do this".
*/
#include <QtTest>

#include <QCoreApplication>
#include <QProcess>
#include <QString>

class TestConsoleFatal : public QObject
{
    Q_OBJECT

private slots:
    /// Redirection is the case that used to lose everything.
    ///
    /// A GUI-subsystem binary has no console, so the console modes attach to
    /// the parent's - and the attach reopened stdout on CONOUT$, which is the
    /// *screen buffer*, not the pipe the user asked for. `traduku --translate
    /// | more`, `traduku --selftest > report.txt` and the harness's own
    /// "capture the output and grep it" all read an empty stream: the report
    /// went to a desktop nobody was looking at. Measured through a pipe, not
    /// from a terminal, because from a terminal the bug hides (there the
    /// process already has a console, so nothing is attached).
    void redirectedStdoutStillGetsTheReport()
    {
        const QString exe = QString::fromUtf8(TRADUKU_EXE);
        QVERIFY(QFileInfo::exists(exe));

        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        p.setProcessEnvironment(env);
        p.start(exe, {QStringLiteral("--version")});
        QVERIFY2(p.waitForStarted(10000), qPrintable(p.errorString()));
        QVERIFY2(p.waitForFinished(15000),
                 "the console mode did not finish while writing to a pipe");
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p.exitCode(), 0);
        QVERIFY2(out.contains(QStringLiteral("traduku")),
                 qPrintable(QStringLiteral("the report never reached the pipe: [")
                            + out + QStringLiteral("]")));
        QVERIFY2(out.contains(QStringLiteral("0.")),   // a version number
                 qPrintable(QStringLiteral("no version in the output: [") + out
                            + QStringLiteral("]")));

        // The same for the longest report the CLI has, which is the one people
        // redirect into a file to attach to a bug report.
        QProcess self;
        self.setProcessEnvironment(env);
        self.start(exe, {QStringLiteral("--selftest")});
        QVERIFY2(self.waitForStarted(10000), qPrintable(self.errorString()));
        QVERIFY2(self.waitForFinished(60000), "no --selftest report on a pipe");
        const QString rows = QString::fromLocal8Bit(self.readAllStandardOutput());
        QVERIFY2(rows.contains(QStringLiteral("SELFTEST")),
                 qPrintable(QStringLiteral("--selftest printed nothing to the "
                                            "pipe: [") + rows + QStringLiteral("]")));
    }

    void anUnusablePlatformIsAConsoleErrorNotADialog()
    {
        const QString exe = QString::fromUtf8(TRADUKU_EXE);
        QVERIFY2(QFileInfo::exists(exe),
                 qPrintable(QStringLiteral("the shell binary was not built: ") + exe));

        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        // A QPA the human asked for and that does not exist: the app cannot
        // invent a screen, but it can refuse in words.
        env.insert(QStringLiteral("QT_QPA_PLATFORM"),
                   QStringLiteral("traduku-no-such-platform"));
        env.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));
        p.setProcessEnvironment(env);
        p.start(exe, {QStringLiteral("--backend-info")});
        QVERIFY2(p.waitForStarted(10000), qPrintable(p.errorString()));
        // Bounded on purpose: a modal dialog is exactly a process that is
        // willing to wait forever, and "it is still running" is the failure
        // this row exists to catch.
        if (!p.waitForFinished(15000)) {
            p.kill();
            p.waitForFinished(3000);
            QFAIL("a console mode did not return: it is waiting on a dialog");
        }

        const QString err = QString::fromLocal8Bit(p.readAllStandardError());
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QVERIFY2(!err.isEmpty(),
                 "it left without a word: an exit code is not a diagnosis");
        QVERIFY2(err.contains(QStringLiteral("platform"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("the message does not name the thing "
                                           "that is missing: ") + err));
        // rc 2 is the "this machine cannot do what you asked" code the CLI
        // already uses; a crash code (abort) is what this row replaced.
        QCOMPARE(p.exitCode(), 2);
    }
};

QTEST_GUILESS_MAIN(TestConsoleFatal)
#include "tst_consolefatal.moc"
