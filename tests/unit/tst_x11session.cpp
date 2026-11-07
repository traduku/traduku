// Finding an X server to talk to from a process that was started by a
// wayland session - the situation Ubuntu 24.04 and Debian 13 actually put us
// in, where the user logs into GNOME or Plasma on Wayland and XWayland is the
// only X server on the machine.
//
// Nothing here opens a connection: DISPLAY/XAUTHORITY resolution is string
// work over environment blocks and /proc cmdlines, injected as data, so it is
// testable on a headless build host. The one exception is
// theProcReadersSeeARunningProcess(), which starts a process that pretends to
// be Xwayland so the /proc readers are exercised against something real.
#include <QtTest>

#include <QTemporaryDir>

#include "platform/x11/X11Clipboard.h"

#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace lt;

class TestX11Session : public QObject
{
    Q_OBJECT
private:
    /// Start a process whose /proc entry says it is an Xwayland serving
    /// `display`: a copy of `fake-xwayland` (a program that only pauses),
    /// renamed, so argv[0] is the program name the parser reads and the rest
    /// of argv is the argument line a real Xwayland would have.
    static pid_t spawnFakeXwayland(const QString &program, const QString &display,
                                   const QString &cookie)
    {
        QList<QByteArray> argBytes;
        argBytes << program.toUtf8() << display.toUtf8()
                 << QByteArray("-auth") << cookie.toUtf8();
        QList<char *> argv;
        for (QByteArray &b : argBytes) {
            argv << b.data();
        }
        argv << nullptr;

        // envp is an ARRAY of NUL-terminated strings terminated by a NULL
        // pointer - not one buffer with NULs inside. Handing it a single
        // QByteArray ("PATH=...\0XAUTHORITY=...\0") is legal memory and the
        // kernel stops at the first NUL, so the child silently got PATH and
        // no XAUTHORITY; the test below is what found that.
        QList<QByteArray> envBytes;
        envBytes << (QByteArray("PATH=") + qgetenv("PATH"))
                 << (QByteArray("XAUTHORITY=") + cookie.toUtf8());
        QList<char *> envp;
        for (QByteArray &b : envBytes) {
            envp << b.data();
        }
        envp << nullptr;

        // Its standard descriptors go to /dev/null: inherited, they hold open
        // whatever pipe the test's own output goes into, and the process
        // outlives the assertions - `./tst | tail` hung on that writer.
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        for (int fd = 0; fd <= 2; ++fd) {
            posix_spawn_file_actions_addopen(&fa, fd, "/dev/null", O_WRONLY, 0);
        }
        pid_t pid = -1;
        const int rc = posix_spawn(&pid, program.toUtf8().constData(), &fa, nullptr,
                                   argv.data(), envp.data());
        posix_spawn_file_actions_destroy(&fa);
        return rc == 0 ? pid : -1;
    }

    pid_t m_child = -1;

private slots:
    void cleanup()
    {
        // A killed-but-unwaited child is a zombie, and a zombie's
        // /proc/<pid>/cmdline reads empty - which the next run of this suite
        // would report as "the process is not in /proc".
        if (m_child > 0) {
            kill(m_child, SIGKILL);
            waitpid(m_child, nullptr, 0);
            m_child = -1;
        }
    }

    void displayComesFromTheEnvironment()
    {
        QCOMPARE(X11Session::displayFromEnvironment(
                     {QStringLiteral("PATH=/bin"), QStringLiteral("DISPLAY=:0")}),
                 QStringLiteral(":0"));
        // A remote DISPLAY is still a DISPLAY: X11Clipboard does not assume local.
        QCOMPARE(X11Session::displayFromEnvironment(
                     {QStringLiteral("DISPLAY=host.example:1.2")}),
                 QStringLiteral("host.example:1.2"));
        QVERIFY(X11Session::displayFromEnvironment(
                    {QStringLiteral("PATH=/bin"), QStringLiteral("DISPLAY=")}).isEmpty());
        // Not DISPLAY= at all - a process started outside any session.
        QVERIFY(X11Session::displayFromEnvironment({QStringLiteral("PATH=/bin")}).isEmpty());
    }

    void displayOfTheXwaylandThatIsActuallyRunning()
    {
        // Plasma's and GNOME's spellings differ, and both forms of the
        // argument appear in the wild: `Xwayland :1 ...` and `-display :1`.
        QCOMPARE(X11Session::displayOfXwayland(
                     {QStringLiteral("/usr/bin/Xwayland :1 -rootless -core 11")}),
                 QStringLiteral(":1"));
        QCOMPARE(X11Session::displayOfXwayland(
                     {QStringLiteral("/usr/bin/Xwayland -display :0 -auth /run/user/1000/xauth_abc")}),
                 QStringLiteral(":0"));
        // Noise that mentions Xwayland but is not it (an editor with the
        // source open, a grep) must not be mistaken for a server.
        QVERIFY(X11Session::displayOfXwayland(
                    {QStringLiteral("vim /usr/src/xwayland/xwayland.c")}).isEmpty());
        QVERIFY(X11Session::displayOfXwayland({}).isEmpty());
    }

    void xauthorityIsReadOutOfTheServingProcessEnvironment()
    {
        // The reason this exists: XWayland's Xauthority lives in a temp file
        // under /run/user, named by the Xwayland process, and a process
        // started by systemd --user (autostart!) never inherits it. Reading
        // it from /proc/<pid>/environ is the only way to authorise.
        QByteArray env;
        env += "PATH=/usr/bin";
        env.append('\0');
        env += "HOME=/home/vm";
        env.append('\0');
        env += "XAUTHORITY=/run/user/1000/gdm/Xauthority";
        env.append('\0');
        QCOMPARE(X11Session::xauthorityFromEnviron(env),
                 QStringLiteral("/run/user/1000/gdm/Xauthority"));
        // Only the exact key counts - a variable that merely ends with the
        // name is somebody else's business.
        QByteArray decoy;
        decoy += "MYXAUTHORITY=/tmp/other";
        decoy.append('\0');
        QVERIFY(X11Session::xauthorityFromEnviron(decoy).isEmpty());
        QVERIFY(X11Session::xauthorityFromEnviron(QByteArray()).isEmpty());
    }

    void candidatesAreOfferedMostSpecificFirst()
    {
        const QStringList c = X11Session::candidateXauthorities(
            QStringLiteral("/home/vm"), 1000, QStringLiteral("/from/env"));
        QCOMPARE(c.first(), QStringLiteral("/from/env"));
        QVERIFY(c.contains(QStringLiteral("/run/user/1000/gdm/Xauthority")));
        QVERIFY(c.contains(QStringLiteral("/home/vm/.Xauthority")));
        // A session that gave us XAUTHORITY must not also drag in a stale
        // file from another seat: nothing is listed twice.
        QCOMPARE(QSet<QString>(c.begin(), c.end()).size(), c.size());
        // No env value: still the two well-known places, gdm before home.
        const QStringList d = X11Session::candidateXauthorities(
            QStringLiteral("/home/vm"), 1000, QString());
        QCOMPARE(d.size(), 2);
        QVERIFY(d.first().startsWith(QStringLiteral("/run/user/1000/")));
    }

    void theProcReadersSeeARunningProcess()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString fake = qEnvironmentVariable("FAKE_XWAYLAND");
        QVERIFY2(!fake.isEmpty(),
                 "built fake-xwayland not passed in (FAKE_XWAYLAND) - the test "
                 "would have nothing to find in /proc");
        const QString exe = dir.path() + QStringLiteral("/Xwayland");
        QFile::remove(exe);
        QVERIFY(QFile::copy(fake, exe));
        QFile::setPermissions(exe, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                      | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                      | QFileDevice::ExeOther);
        const QString cookie = dir.path() + QStringLiteral("/xauth-abcdef");
        QFile cf(cookie);
        QVERIFY(cf.open(QIODevice::WriteOnly));
        cf.write("0");
        cf.close();

        m_child = spawnFakeXwayland(exe, QStringLiteral(":99"), cookie);
        QVERIFY2(m_child > 0, "could not start the fake Xwayland");

        // Match on the argument we passed, not on argv[0]: the kernel hands
        // argv[0] back resolved, and /tmp is a symlink on this host, so the
        // path we started with is not the string that comes back. Poll
        // because a pid exists before exec has filled cmdline in.
        QString ours;
        for (int i = 0; i < 100 && ours.isEmpty(); ++i) {
            for (const QString &c : X11Session::readProcCmdlines()) {
                if (c.contains(cookie)) {
                    ours = c;
                    break;
                }
            }
            if (ours.isEmpty()) {
                QTest::qWait(20);
            }
        }
        QVERIFY2(!ours.isEmpty(),
                 qPrintable(QStringLiteral("pid %1 never became visible in /proc")
                            .arg(m_child)));
        QCOMPARE(X11Session::displayOfXwayland({ours}), QStringLiteral(":99"));
        // The machine-wide scan finds it too, unless the machine has its own
        // Xwayland (a grid guest does) - then the scan answers for that one.
        QStringList all = X11Session::readProcCmdlines();
        all.removeAll(ours);
        if (X11Session::displayOfXwayland(all).isEmpty()) {
            QCOMPARE(X11Session::displayOfXwayland(X11Session::readProcCmdlines()),
                     QStringLiteral(":99"));
        }

        // The cookie file is named only in that process's environment, which
        // is the whole trick: nothing on disk points at it by a predictable
        // name. Read it back through the same path the app uses.
        QByteArray environ;
        for (int i = 0; i < 50 && !environ.contains("XAUTHORITY="); ++i) {
            environ = X11Session::readProcEnviron(m_child);
            if (!environ.contains("XAUTHORITY=")) {
                QTest::qWait(20);
            }
        }
        QVERIFY2(environ.contains("XAUTHORITY="),
                 "the child's /proc environ carries no XAUTHORITY - the spawn "
                 "did not pass it, or the read raced exec");
        QCOMPARE(X11Session::xauthorityFromEnviron(environ), cookie);
        QCOMPARE(X11Session::resolveXauthority(QStringLiteral(":99")), cookie);
    }

    void aDisplayInTheEnvironmentWinsOverEverything()
    {
        qputenv("DISPLAY", ":7");
        QCOMPARE(X11Session::resolveDisplay(), QStringLiteral(":7"));
        qunsetenv("DISPLAY");
    }
};

QTEST_GUILESS_MAIN(TestX11Session)
#include "tst_x11session.moc"
