#include <QtTest>
#include <QCoreApplication>

#include "app/cli.h"
#include "app/AppFlavour.h"
#include <QGuiApplication>

using namespace lt;

// The CLI used to live inside src/app/kde/main.cpp, which meant the GNOME
// shell would have to duplicate it (and did already duplicate the bug:
// `traduku --version` printed no version because nobody called
// QApplication::setApplicationVersion). Both shells now share this parser,
// so it gets tests of its own.
class TestCli : public QObject
{
    Q_OBJECT
    QCoreApplication *m_app = nullptr;

    CliOptions parse(const QStringList &args, const QString &shell = QStringLiteral("gnome"))
    {
        return parseCommandLine(*m_app, QStringList{QStringLiteral("traduku")} + args, shell);
    }

private slots:
    void initTestCase()
    {
        static int argc = 1;
        static char arg0[] = "traduku";
        static char *argv[] = {arg0, nullptr};
        m_app = new QCoreApplication(argc, argv);
    }
    void cleanupTestCase() { delete m_app; m_app = nullptr; }

    void theLauncherAndTheAppAgreeOnOneName()
    {
        // QGuiApplication::setDesktopFileName is what Qt reports as the
        // Wayland app_id, and StartupWMClass in the .desktop is what GNOME
        // matches an X11 window against. Both come from lt::desktopBaseName()
        // now; written by hand in three places they drifted apart once
        // already (tests/cmake/test_versions_agree.sh guards the packaging
        // side, tests/e2e/test_09 the window the compositor actually sees).
        configureApplication(*m_app, QStringLiteral("gnome"),
                             QStringLiteral("0.0.0-test"));
        QCOMPARE(lt::desktopBaseName(), QString("traduku"));
        // No ".desktop" suffix: Qt removes it and writes a warning to stderr
        // when you pass one, and stderr here is the systemd journal of an
        // autostarted process.
        QCOMPARE(QGuiApplication::desktopFileName(), QString("traduku"));
    }

    void noArgsMeansTrayMode()
    {
        const CliOptions o = parse({});
        QVERIFY2(o.errors.isEmpty(), qPrintable(o.errors.join("; ")));
        QCOMPARE(int(o.mode), int(CliOptions::Mode::Tray));
        QCOMPARE(o.exitCode, 0);
    }

    void translateModeAcceptsShortAndLongForm()
    {
        QCOMPARE(int(parse({QStringLiteral("--translate")}).mode),
                 int(CliOptions::Mode::Translate));
        QCOMPARE(int(parse({QStringLiteral("-t")}).mode),
                 int(CliOptions::Mode::Translate));
    }

    // The tray's "Launch on Login" is the only way to change autostart, and
    // on GNOME it lives in a panel icon that needs a shell extension to be
    // visible at all. When that icon is missing - the exact situation of the
    // user who reaches for a terminal - the control must exist somewhere
    // reachable.
    void autostartModeTakesOnOffOrStatus()
    {
        const CliOptions off = parse({QStringLiteral("--autostart=off")});
        QCOMPARE(int(off.mode), int(CliOptions::Mode::Autostart));
        QCOMPARE(off.autostartValue, QString("off"));
        QCOMPARE(off.exitCode, 0);
        QCOMPARE(parse({QStringLiteral("--autostart=on")}).autostartValue,
                 QString("on"));
        QCOMPARE(parse({QStringLiteral("--autostart"), QStringLiteral("status")}).autostartValue,
                 QString("status"));

        // A typo must not quietly start a tray app: the user typed a command
        // and deserves the error, not a process that never exits.
        const CliOptions bad = parse({QStringLiteral("--autostart=yes-please")});
        QVERIFY(!bad.errors.isEmpty());
        QVERIFY2(bad.errors.join(QStringLiteral("; ")).contains(QStringLiteral("status")),
                 qPrintable(bad.errors.join(QStringLiteral("; "))));
        QVERIFY(bad.exitCode != 0);
    }

    void translateCarriesExplicitText()
    {
        const CliOptions o = parse({QStringLiteral("--translate"),
                                    QStringLiteral("--text"), QStringLiteral("Hello world")});
        QCOMPARE(int(o.mode), int(CliOptions::Mode::Translate));
        QCOMPARE(o.text, QStringLiteral("Hello world"));
        QVERIFY(o.errors.isEmpty());
    }

    void translateWithoutTextMeansClipboard()
    {
        const CliOptions o = parse({QStringLiteral("--translate")});
        QVERIFY(o.text.isNull());
    }

    void selftestModeAndLiveFlag()
    {
        const CliOptions o = parse({QStringLiteral("--selftest"), QStringLiteral("--live")});
        QCOMPARE(int(o.mode), int(CliOptions::Mode::SelfTest));
        QVERIFY(o.live);
    }

    void liveOnlyMakesSenseWithSelftest()
    {
        const CliOptions o = parse({QStringLiteral("--live")});
        QVERIFY2(!o.errors.isEmpty(), "--live alone was accepted");
        QVERIFY(o.errors.join(QLatin1Char(' ')).contains(QStringLiteral("--selftest")));
    }

    void backendInfoMode()
    {
        QCOMPARE(int(parse({QStringLiteral("--backend-info")}).mode),
                 int(CliOptions::Mode::BackendInfo));
    }

    void configPathOption()
    {
        const CliOptions o = parse({QStringLiteral("--translate"),
                                    QStringLiteral("--config"),
                                    QStringLiteral("/tmp/other.ini"),
                                    QStringLiteral("--text"), QStringLiteral("hi")});
        QVERIFY2(o.errors.isEmpty(), qPrintable(o.errors.join(QLatin1Char(';'))));
        QCOMPARE(o.configPath, QStringLiteral("/tmp/other.ini"));
    }

    void configPathDefaultsEmpty()
    {
        QVERIFY(parse({QStringLiteral("--translate")}).configPath.isEmpty());
    }

    void notifyTestMode()
    {
        // Support and e2e need one way to ask "does a bubble actually
        // appear" without waiting for a translation to fail.
        QCOMPARE(int(parse({QStringLiteral("--notify-test")}).mode),
                 int(CliOptions::Mode::NotifyTest));
    }

    void modesAreMutuallyExclusive()
    {
        const CliOptions o = parse({QStringLiteral("--translate"),
                                    QStringLiteral("--selftest")});
        QVERIFY2(!o.errors.isEmpty(), "two modes at once was accepted");
        QVERIFY(o.exitCode != 0);
    }

    void textWithoutTranslateIsRejected()
    {
        // catches `traduku --text foo` silently starting the tray
        const CliOptions o = parse({QStringLiteral("--text"), QStringLiteral("foo")});
        QVERIFY2(!o.errors.isEmpty(), "--text alone was accepted");
    }

    void unknownOptionIsReportedNotSwallowed()
    {
        const CliOptions o = parse({QStringLiteral("--translaet")});
        QVERIFY(!o.errors.isEmpty());
        QVERIFY(o.errors.join(QLatin1Char(' ')).contains(QStringLiteral("translaet")));
        QVERIFY(o.exitCode != 0);
    }

    void helpAndVersionAreFlagsNotExits()
    {
        // parser.process() would quit() the process - unusable in tests and
        // in --backend-info. parse() + flags lets main() print and return.
        QVERIFY(parse({QStringLiteral("--help")}).wantsHelp);
        QVERIFY(parse({QStringLiteral("--version")}).wantsVersion);
    }

    void versionIsActuallySet()
    {
        // Regression: --version printed "traduku" with no number.
        configureApplication(*m_app, QStringLiteral("gnome"), QStringLiteral("9.9.9"));
        QCOMPARE(QCoreApplication::applicationVersion(), QStringLiteral("9.9.9"));
        QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("traduku"));
        QCOMPARE(prettyShellName(QStringLiteral("gnome")), QStringLiteral("GNOME"));
        QCOMPARE(prettyShellName(QStringLiteral("kde")), QStringLiteral("KDE Plasma"));
        // The Windows shell must introduce itself as Windows: a report whose
        // own header is wrong is a report nobody trusts afterwards.
        QCOMPARE(prettyShellName(QStringLiteral("windows")), QStringLiteral("Windows"));
    }

    void panelNameIsTheSameOnBothShells()
    {
        // The KDE shell set the display name, the GNOME shell did not: two
        // shells configuring the same product differently, which is what
        // configureApplication exists to stop. Measured (and the reason the
        // e2e asserts the tray identity on the ToolTip instead): with the
        // display name set, Plasma's panel shows Title/Id "Traduku" while the
        // Ubuntu guest's Qt 6.8 still reports the lowercase applicationName.
        // What is not a Qt detail is this - both shells now say "Traduku",
        // and the machine-readable name stays lowercase.
        configureApplication(*m_app, QStringLiteral("gnome"), QStringLiteral("9.9.9"));
        QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("Traduku"));
        configureApplication(*m_app, QStringLiteral("kde"), QStringLiteral("9.9.9"));
        QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("Traduku"));
        // ... and the machine-readable id stays the lowercase one (config
        // file, D-Bus name and package name all use it).
        QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("traduku"));
    }
};

QTEST_APPLESS_MAIN(TestCli)
#include "tst_cli.moc"
