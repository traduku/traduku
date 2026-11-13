// traduku: Windows shell around the platform-agnostic core.
//
// Composition only: settings, backends (gesture/clipboard/notifier/sounds/
// autostart), the AppController and the tray. No business logic here, and -
// exactly as on the other shells - no OS call outside src/platform/win.
//
// What is Windows about this file:
//   gesture   WH_KEYBOARD_LL, the only observer that sees Ctrl+C+C without
//             taking it away from the application being typed into;
//   clipboard Qt's own, which here is a real win32 client rather than the
//             blind Wayland fallback;
//   autostart a value in HKCU\...\CurrentVersion\Run, plus the machine-wide
//             key read as "something else may be starting us";
//   sounds    %WINDIR%\Media through WMF, with a detached powershell behind it;
//   console   a GUI-subsystem binary that still answers `traduku --translate`
//             from cmd (WinConsole.h).
//
// What is deliberately NOT here: a second clipboard reader, a second gesture
// state machine, a second CLI, a second selftest. Those exist once, above this
// file, and the gate in ROADMAP.md §7 W5 is what keeps that true.

#include "app/AppController.h"
#include "app/AppFlavour.h"
#include "app/BackendInfo.h"
#include "app/PlatformBackends.h"
#include "app/SelfTest.h"
#include "app/SingleInstance.h"
#include "app/TrayChoice.h"
#include "platform/win/WinClipboardPolicy.h"
#include "app/cli.h"
#include "core/chunkedtranslator.h"
#include "core/settings.h"
#include "core/soundplayer.h"
#include "interfaces/iautostart.h"
#include "interfaces/iclipboard.h"
#include "ui/SettingsDialog.h"
#include "ui/TranslatorWindow.h"
#include "platform/qt/QtTray.h"
#include "platform/win/WinHotkeyBackend.h"
#include "platform/win/WinKeyHook.h"
#include "platform/win/WinConsole.h"
#include "platform/win/WinNotifier.h"
#include "platform/win/WinSessionInfo.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMessageBox>
#include <QShortcut>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>

#include <cstdlib>
#include <memory>

using namespace lt;

namespace {

/// The gesture, started for real. `--backend-info` has to be able to say
/// "installed" rather than "this build is supposed to install a hook", so the
/// probe runs the same start()/stop() the app runs (ROADMAP.md §4.1b: the
/// report is produced by the decision, not written beside it).
struct GestureSetup
{
    std::unique_ptr<WinHotkeyBackend> hook;
    IHotkeyBackend *chosen = nullptr;
};

GestureSetup createGesture(Settings &settings)
{
    GestureSetup g;
    g.hook = std::make_unique<WinHotkeyBackend>(winKeyHookInstaller(),
                                                winKeyHookUninstaller());
    g.hook->setIntervalMs(qint64(settings.pressInterval() * 1000.0));
    if (g.hook->start()) {
        g.chosen = g.hook.get();
    }
    return g;
}

BackendReport collectBackends(Settings *settings)
{
    BackendReport r;
    r.shell = QStringLiteral("windows");

    const WinSessionFacts facts = queryWinSessionFacts();
    r.sessionType = winSessionString(facts);
    r.desktop = QStringLiteral("Windows %1").arg(QSysInfo::productVersion());

    auto cb = createDefaultClipboard(QGuiApplication::clipboard());
    r.clipboard = cb->backendName();
    r.clipboardHint = cb->diagnostic();
    if (r.clipboardHint.isEmpty()) {
        // The one knob this OS cannot honour, said out loud rather than
        // ignored: a debugging handle that changes nothing looks like a
        // clipboard that works.
        r.clipboardHint = windowsClipboardProblem(
            qEnvironmentVariable("TRADUKU_CLIPBOARD"));
    }

    Settings probeSettings;   // --backend-info must not need a config file
    GestureSetup gesture = createGesture(probeSettings);
    if (gesture.chosen) {
        r.hotkeyAttempts.append({QStringLiteral("win-ll-hook"),
                                 QStringLiteral("chosen")});
        r.hotkeyChosen = gesture.chosen->backendName();
        r.gestureSource = QStringLiteral("WH_KEYBOARD_LL (passive, no grab)");
        gesture.hook->stop();
    } else {
        r.hotkeyAttempts.append({QStringLiteral("win-ll-hook"),
                                 gesture.hook->lastError()});
        r.inputAccess = gesture.hook->lastError();
    }

    // The notifier's name is a claim about the shell, so it is asked of the
    // same object the app uses, before anything has been notified.
    WinNotifier probe(WinNotifier::trayBalloonSender());
    r.notifier = probe.backendName();

    r.tray = QSystemTrayIcon::isSystemTrayAvailable()
        ? trayDisplayName(QStringLiteral("qt"))
        : trayDisplayName(QStringLiteral("none"));

    r.reasoning = settings && settings->disableReasoning()
        ? QStringLiteral("skipped (request options sent)")
        : QStringLiteral("NOT disabled - the model may stream hidden reasoning");

    if (settings) {
        std::unique_ptr<SoundPlayer> sound(createDefaultSoundPlayer(settings));
        const SoundReport rep = soundReport(*sound);   // one function, every shell
        r.audio = rep.summary;
        r.audioCues = rep.cues;
    }

    r.autostart = createDefaultAutostart(QCoreApplication::applicationFilePath())
                      ->diagnostic();
    return r;
}

int doNotifyTest()
{
    QTextStream out(stdout);
    WinNotifier notifier(WinNotifier::trayBalloonSender());
    notifier.notify(QStringLiteral("Traduku"),
                    QStringLiteral("If you can read this, Windows notifications "
                                   "work. (traduku --notify-test)"));
    if (notifier.delivered() > 0) {
        out << "notification service: accepted (" << notifier.backendName()
            << ")\n";
        return 0;
    }
    out << "notification service: not delivered - "
        << (notifier.errorText().isEmpty() ? QStringLiteral("no reason given")
                                           : notifier.errorText())
        << '\n';
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QStringList argvList;
    argvList.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        argvList << QString::fromLocal8Bit(argv[i]);
    }

    // The platform answers, not the X11 variables: on a logged-in Windows
    // desktop a console mode may use the real platform, and then
    // --backend-info reports the tray that is actually there instead of
    // "none (no tray host)" - which it said only because we had forced
    // offscreen by asking a Unix question.
    lt::prepareCliEnvironment(argvList, lt::platformHasInteractiveDesktop());

    // The console modes of a GUI binary. Decided by the same pure rule the unit
    // suite pins, carried out here (WinConsole.h).
    if (lt::decideConsoleAttach(/*onWindows=*/true, winHasConsole(),
                               /*trayApp=*/!lt::printsToTerminal(argvList))
        == lt::ConsoleDecision::Attach) {
        winAttachToParentConsole();
    }

    // Qt's default handler on Windows sends qWarning() to OutputDebugString
    // when the process has no console of its own - which a GUI binary never
    // does - so the advice below (and every later diagnostic in a console mode)
    // reached a debugger instead of the person who typed the command. Measured:
    // `--selftest` with no usable desktop exited 2 having printed nothing.
    // The line itself is formatted by the tested pure rule; only the fputs is
    // from here, and it is installed in the shell rather than in platform/win
    // because this is the one target that may see both halves.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &,
                              const QString &text) {
        const char *name = "debug";
        switch (type) {
        case QtDebugMsg: name = "debug"; break;
        case QtInfoMsg: name = "info"; break;
        case QtWarningMsg: name = "warning"; break;
        case QtCriticalMsg: name = "critical"; break;
        case QtFatalMsg: name = "fatal"; break;
        }
        const QString line = lt::qtConsoleMessage(QLatin1String(name), text);
        if (line.isEmpty()) {
            return;
        }
        fputs(qPrintable(line), stderr);
        fputc('\n', stderr);
        fflush(stderr);
        if (type == QtFatalMsg) {
            // qFatal's contract is "print, then abort" - and for a
            // GUI-subsystem binary, abort means a Windows error dialog and a
            // process content to wait for a click that a service session never
            // delivers (a stray traduku.exe behind an invisible window, killed
            // by hand; tst_consolefatal catches it as a process that never
            // returns). The line above already says what is missing and what to
            // do about it, so leaving with the CLI's "this machine cannot do
            // that" code is the honest ending - a crash code is an accident,
            // not an answer.
            _exit(2);
        }
    });

    // A QT_QPA_PLATFORM that names an absent plugin used to end the same way:
    // Qt's fatal message, then the dialog. Asking the directory instead of Qt
    // costs a readdir and buys a sentence.
    {
        QStringList installed;
        const QString suffix = QStringLiteral(".dll");
        QStringList dirs;
        // Where plugins can be: beside this exe (a windeployqt package), and
        // Qt's own tree when it is still installed - the development case.
        // Asked of the OS and of QLibraryInfo, never of QCoreApplication:
        // none of that exists yet, and asking prints "please instantiate the
        // QApplication object first" and answers with the *current directory* -
        // which once told a complete package that it had no plugins at all.
        const QString self = lt::winModuleFilePath();
        if (!self.isEmpty()) {
            dirs << QFileInfo(self).absolutePath() + QStringLiteral("/platforms");
        }
        const QString qtPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
        if (!qtPlugins.isEmpty() && QDir(qtPlugins).exists()) {
            dirs << qtPlugins + QStringLiteral("/platforms");
        }
        for (const QString &d : dirs) {
            for (const QString &f : QDir(d).entryList(
                     QStringList{QStringLiteral("q*") + suffix}, QDir::Files)) {
                installed << f.mid(1, f.size() - 1 - suffix.size());
            }
        }
        installed.removeDuplicates();
        if (const QString why = lt::platformPluginProblem(
                qEnvironmentVariable("QT_QPA_PLATFORM"), installed);
            !why.isEmpty()) {
            QTextStream(stderr) << why << "\n";
            return 2;
        }
    }

    // The desktop question, of the OS that owns it. Asking
    // AppFlavour::qtCanPickAPlatform() here (DISPLAY, WAYLAND_DISPLAY) made
    // every Windows desktop answer "no display": the GUI mode printed advice
    // into a terminal that a double-click does not have, and exited. tst_wingui
    // runs the real exe with no arguments in both kinds of session.
    if (const QString noDisplay = lt::displayAdviceForMissingDisplay(
            argvList, lt::platformHasInteractiveDesktop(), /*onWindows=*/true);
        !noDisplay.isEmpty()) {
        if (lt::guiRefusalNeedsDialog(winHasConsole())) {
            // Started by double-click: there is no terminal for this sentence,
            // so it goes on the screen - and leaves by itself if nobody is
            // sitting at this machine.
            winShowModalError(QStringLiteral("Traduku"), noDisplay, 30000);
        } else {
            qWarning().noquote() << noDisplay;
        }
        return 2;
    }

    std::unique_ptr<QCoreApplication> app(
        lt::needsWidgets(argvList)
            ? static_cast<QCoreApplication *>(new QApplication(argc, argv))
            : static_cast<QCoreApplication *>(new QGuiApplication(argc, argv)));
    if (auto *widgetApp = qobject_cast<QApplication *>(app.get())) {
        widgetApp->setQuitOnLastWindowClosed(false);   // tray-only app
    }
    configureApplication(*app, QStringLiteral("windows"), QStringLiteral(LT_VERSION));

    const CliOptions cli = parseCommandLine(*app, argvList, QStringLiteral("windows"));
    QTextStream out(stdout), err(stderr);

    if (cli.wantsHelp) {
        out << cliHelpText(*app, QStringLiteral("windows")) << '\n';
        return 0;
    }
    if (cli.wantsVersion) {
        out << QStringLiteral("traduku %1 (%2 shell)\n")
                   .arg(QCoreApplication::applicationVersion(),
                        prettyShellName(QStringLiteral("windows")));
        return 0;
    }
    if (cli.exitCode != 0) {
        for (const QString &e : cli.errors) {
            err << "traduku: " << e << '\n';
        }
        err << "try --help\n";
        return cli.exitCode;
    }

    std::unique_ptr<Settings> settingsHolder(cli.configPath.isEmpty()
                                                 ? new Settings
                                                 : new Settings(cli.configPath));
    Settings &settings = *settingsHolder;

    switch (cli.mode) {
    case CliOptions::Mode::BackendInfo:
        out << formatBackendInfo(collectBackends(&settings));
        return 0;
    case CliOptions::Mode::SelfTest:
        return runSelfTest(out, cli.live, cli.configPath) == 0 ? 0 : 1;
    case CliOptions::Mode::Translate:
        return runOneShotMode(*app, settings, cli);
    case CliOptions::Mode::NotifyTest:
        return doNotifyTest();
    case CliOptions::Mode::Autostart:
        return runAutostartCommand(cli.autostartValue);
    case CliOptions::Mode::Tray:
        break;
    }

    // ---- tray mode ------------------------------------------------------
    SingleInstance instance;      // the named pipe is the token here
    if (!instance.start()) {
        const lt::SingleInstanceVerdict verdict =
            lt::singleInstanceVerdict(instance.errorText());
        out << verdict.message;
        out.flush();
        return verdict.exitCode;
    }

    // A Traduku that cannot hear the keyboard is not a Traduku. On this OS the
    // hook has one failure mode (no interactive desktop), and when it happens
    // the app refuses instead of running deaf.
    BackendReport report;
    GestureSetup gesture = createGesture(settings);
    if (!gesture.chosen) {
        report.shell = QStringLiteral("windows");
        report.sessionType = winSessionString(queryWinSessionFacts());
        err << "traduku: no gesture backend available - Ctrl+C+C would do "
               "nothing.\n";
        err << formatBackendInfo(report);
        return 5;
    }

    // Which tray this build must create: LT_TRAY at build time (a packaging
    // default - the gate test tests/cmake/test_lt_tray_option.sh checks that
    // the define actually reaches this target) and TRADUKU_TRAY per run, so an
    // operator can compare the two paths in one command. Honouring it is not
    // decoration: "none" is how a user with a broken notification-area host, or
    // a kiosk that must never show an icon, runs the app at all.
    const TrayChoice trayChoice = resolveTrayKind(
        QLatin1String(LT_TRAY_DEFAULT), qEnvironmentVariable("TRADUKU_TRAY"));
    if (!trayChoice.warning.isEmpty()) {
        err << "traduku: " << trayChoice.warning << "\n";
    }

    // The tray first: the notifier's balloon needs the icon to be drawn on,
    // and a notification path that silently has no host is the failure the
    // delivered/dropped counters exist to make visible.
    std::unique_ptr<IAutostart> autostart =
        createDefaultAutostart(QCoreApplication::applicationFilePath());
    std::unique_ptr<QtTray> tray;
    if (trayChoice.kind == QLatin1String("qt")) {
        tray = std::make_unique<QtTray>(&settings, autostart.get());
    }
    WinNotifier notifier([&tray](const QString &title, const QString &body, int ms) {
        QSystemTrayIcon *icon = tray ? tray->icon() : nullptr;
        if (!icon || !QSystemTrayIcon::isSystemTrayAvailable()) {
            return false;
        }
        icon->showMessage(title, body, QSystemTrayIcon::Information,
                          ms > 0 ? ms : 5000);
        return true;
    });

    std::unique_ptr<SoundPlayer> sound(createDefaultSoundPlayer(&settings));
    auto clipboard = createDefaultClipboard(QGuiApplication::clipboard());
    TranslatorWindow window;
    ChunkedTranslator pipeline(&settings);
    AppController controller({&settings, clipboard.get(), &notifier, sound.get(),
                              &window, &pipeline});
    controller.setHotkey(gesture.chosen);
    QObject::connect(&instance, &SingleInstance::activateRequested, &controller,
                     &AppController::surfaceWindow);

    auto openSettings = [&settings]() {
        SettingsDialog dlg(&settings);
        dlg.exec();
    };
    QObject::connect(&controller, &AppController::settingsRequested, app.get(), openSettings);

    if (tray) {
        QObject::connect(tray.get(), &QtTray::translateRequested, &controller,
                         &AppController::translateNow);
        QObject::connect(tray.get(), &QtTray::settingsRequested, app.get(), openSettings);
        QObject::connect(tray.get(), &QtTray::quitRequested, app.get(),
                         &QCoreApplication::quit);
        tray->setToolTipSub(
            QStringLiteral("Press Ctrl+C twice to translate the clipboard"));
        // The kind the *user* asked for, not the one this build was compiled
        // with: an operator comparing TRADUKU_TRAY=none with =qt has to be able
        // to tell from the tooltip which one is running.
        tray->setStatusLabel(lt::trayStatusLabel(
            QCoreApplication::applicationVersion(), trayChoice.kind));
        tray->show();
    }

    // ---- first run ------------------------------------------------------
    // Nothing was installed and no Run value exists, so this is the shell that
    // has to ASK (ROADMAP.md §7.5, question 2 answered as "ask", the Plasma
    // rule): no login entry is written without a yes, and the greeting is the
    // one moment a user learns that Ctrl+C twice is the whole interface.
    QTimer::singleShot(0, app.get(), [&]() {
        if (settings.firstRunDone()) {
            return;
        }
        settings.setFirstRunDone(true);
        settings.save();
        const FirstRunPlan plan = lt::planFirstRun(
            QStringLiteral("windows"), tray && tray->isAvailable(),
            !settings.apiUrl().trimmed().isEmpty());
        if (plan.greet) {
            QString body = QStringLiteral(
                "Press Ctrl+C twice quickly to translate the clipboard (a single "
                "copy is left alone).");
            if (plan.explainMissingIcon) {
                body += QStringLiteral(
                    "\n\nNo icon near the clock? Windows hides new tray icons "
                    "behind the caret in the notification area.");
            }
            notifier.notify(QStringLiteral("Traduku is installed"), body);
        }
        if (plan.askAboutAutostart) {
            const auto answer = QMessageBox::question(
                nullptr, QStringLiteral("Traduku"),
                QStringLiteral("Start Traduku when you log in?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer == QMessageBox::Yes) {
                autostart->setEnabled(true);
            }
        }
        if (plan.openSettings) {
            SettingsDialog dlg(&settings);
            dlg.exec();
        }
    });

    QObject::connect(gesture.chosen, &IHotkeyBackend::activated, app.get(),
                     []() {
                         qInfo("traduku: gesture detected, translating the clipboard");
                     });
    QObject::connect(&pipeline, &ChunkedTranslator::startedDocument, app.get(),
                     [&tray](const QString &source, const QString &target) {
                         qInfo("traduku: translating %d chars -> %s",
                               source.size(), qUtf8Printable(target));
                         if (tray) { tray->setBusy(true); }
                     });
    QObject::connect(&pipeline, &ChunkedTranslator::allDone, app.get(),
                     [&tray]() {
                         qInfo("traduku: translation ready");
                         if (tray) { tray->setBusy(false); }
                     });
    QObject::connect(&pipeline, &ChunkedTranslator::failed, app.get(),
                     [&tray](const QString &reason) {
                         qInfo("traduku: translation failed: %s", qUtf8Printable(reason));
                         if (tray) { tray->setBusy(false); }
                     });

    auto *quit = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), &window);
    QObject::connect(quit, &QShortcut::activated, app.get(), []() { qApp->quit(); });

    // The same sentence the Linux shells log and the harness waits for
    // (AppFlavour.h): shell, session, and the backends actually chosen.
    lt::ReadyInfo ready;
    ready.shell = QStringLiteral("windows");
    ready.session = winSessionString(queryWinSessionFacts());
    ready.clipboard = clipboard->backendName();
    ready.hotkey = gesture.chosen ? gesture.chosen->backendName() : QString();
    ready.trayAvailable = tray && tray->isAvailable();
    lt::logReady(ready);

    out << QStringLiteral("Traduku is running (Windows): press Ctrl+C twice to "
                          "translate. Ctrl+Q quits.\n");
    if (!ready.trayAvailable) {
        out << QStringLiteral("traduku: no tray host on this session - running "
                              "tray-less.\n");
    }
    out.flush();
    return app->exec();
}
