// traduku: KDE Plasma shell around the platform-agnostic core.
//
// Composition only: settings, backends (clipboard/hotkey/notifier/sounds/
// autostart), the AppController and the tray. No business logic here.

#include "app/AppController.h"
#include "app/AppFlavour.h"
#include "app/BackendInfo.h"
#include "app/SelfTest.h"
#include "app/TrayChoice.h"
#include "app/cli.h"
#include "core/llmstreamclient.h"
#include "core/settings.h"
#include "core/shortcutmonitor.h"
#include "core/soundplayer.h"
#include "core/chunkedtranslator.h"
#include "core/translationengine.h"
#include "platform/kde/KdeGlobalShortcut.h"
#include "platform/kde/KdeNotifier.h"
#include "platform/qt/QtClipboard.h"
#include "platform/qt/QtTray.h"
#include "platform/qt/SessionInfo.h"
#include "platform/qt/XdgAutostart.h"
#include "platform/wl/ClipboardFactory.h"
#include "ui/SettingsDialog.h"
#include "ui/TranslatorWindow.h"

#include <KDBusService>

#include <QDBusConnection>

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>
#include <memory>

// X11 headers clash with Qt's (Status/None/True/False): include LAST.
#ifdef LT_HAVE_X11
#include "platform/x11/X11HotkeyBackend.h"
#endif

using namespace lt;

namespace {

/// The tray the shell shows is configuration, not code (ROADMAP.md §2).
/// LT_TRAY=none / TRADUKU_TRAY=none gets a tray that is not there (kiosk and
/// --translate-only deployments). Returning an object instead of nullptr keeps
/// every `connect(tray, ...)` below unconditional, so nobody has to remember
/// which of them needs a null check.
class HiddenTray : public ITray
{
public:
    using ITray::ITray;
    QMenu *menu() const override { return nullptr; }
    void setBusy(bool) override {}
    void setToolTipSub(const QString &) override {}
    void show() override {}
    void setStatusLabel(const QString &) override {}
    bool isAvailable() const override { return false; }
    QString backendName() const override { return QStringLiteral("none"); }
};

/// One tray for both shells. KdeTray (KStatusNotifierItem) was removed once
/// the Qt tray matched it entry for entry in a live Plasma session; KF6's item
/// additionally segfaulted when destroyed with a live session bus, which is
/// exactly what `Quit` did in the released 0.2 build.
ITray *createTray(const QString &kind, Settings *settings, IAutostart *autostart)
{
    if (kind == QLatin1String("none")) {
        return new HiddenTray;
    }
    return new QtTray(settings, autostart);
}

/// What `traduku --backend-info` says on Plasma.
///
/// The KDE shell used not to have this option at all: the shared parser in
/// app/cli.cpp got it in Stage 1 and this main() kept its own QCommandLineParser
/// from before that, so `--backend-info` answered "Unknown option" on the
/// desktop the program is oldest on. The e2e install test is what caught it -
/// it asks both shells the same question and the KDE one could not answer.
BackendReport collectBackends(Settings *settings)
{
    BackendReport r;
    r.shell = QStringLiteral("kde");
    // Session type, not the QPA plugin name: a forced offscreen platform must
    // not make a Wayland box look like it has no Wayland (tests/e2e asserts).
    r.sessionType = sessionType();
    if (r.sessionType.isEmpty()) {
        r.sessionType = QGuiApplication::platformName();
    }
    if (qpaWasForcedByUs()) {
        // We handed this process a QPA because the environment had none. Plain
        // "offscreen" reads as "you have no session"; what is actually missing
        // is our knowledge of it - ssh inherits no XDG_SESSION_TYPE.
        r.sessionType = QStringLiteral("%1 (no display here)").arg(r.sessionType);
    }
    r.desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");

    auto cb = createClipboard(r.sessionType, {}, QGuiApplication::clipboard());
    r.clipboard = cb->backendName();
    r.clipboardHint = cb->diagnostic();
    if (r.clipboardHint.isEmpty()) {
        // A forced TRADUKU_CLIPBOARD that did not take effect is the one case
        // where the chosen backend has nothing to complain about and the user
        // still has a question.
        r.clipboardHint = forcedBackendProblem(r.clipboard);
    }

#ifdef LT_HAVE_X11
    // Conditions, not a probe: starting the X listener would spawn a child
    // process just to answer a question. But the *decision* is the app's own
    // (probeMode), not a paraphrase of it: this branch used to print
    // "x11/xrecord (helper: ...)" whenever DISPLAY was set and the helper
    // existed, which on a native X11 Plasma described RECORD while the app ran
    // the XI2 worker - and the XI2 worker was itself the thing that did not
    // work there, so the report hid the failure twice over.
    if (qEnvironmentVariableIsEmpty("DISPLAY")) {
        r.hotkeyAttempts.append({QStringLiteral("x11"),
                                 QStringLiteral("skipped: no DISPLAY")});
    } else {
        const auto mode = X11HotkeyBackend::probeMode();
        const QString name = X11HotkeyBackend::modeName(mode);
        const QString helper = X11HotkeyBackend::helperPath();
        if (mode == X11HotkeyBackend::Mode::Disabled) {
            r.hotkeyAttempts.append({name,
                                     QStringLiteral("unavailable: no X server "
                                                    "reachable from this session")});
        } else if (mode == X11HotkeyBackend::Mode::Record && helper.isEmpty()) {
            r.hotkeyAttempts.append({name,
                                     QStringLiteral("unavailable: traduku-record-keys "
                                                    "is not installed next to the app "
                                                    "(traduku-gnome does not ship it)")});
        } else {
            r.hotkeyAttempts.append({name, QStringLiteral("chosen")});
            r.hotkeyChosen = name;
            if (mode == X11HotkeyBackend::Mode::Record) {
                r.hotkeyChosen += QStringLiteral(" (helper: %1)").arg(helper);
                r.gestureSource = QStringLiteral("X RECORD on %1")
                                      .arg(qEnvironmentVariable("DISPLAY"));
            } else {
                r.gestureSource = QStringLiteral("XI2 raw on %1 (slave devices)")
                                      .arg(qEnvironmentVariable("DISPLAY"));
            }
        }
    }
#endif
    if (r.hotkeyChosen.isEmpty()) {
        const bool bus = QDBusConnection::sessionBus().isConnected();
        r.hotkeyAttempts.append({QStringLiteral("kglobalaccel"),
                                 bus ? QStringLiteral("chosen")
                                     : QStringLiteral("unavailable: no session bus")});
        if (bus) {
            r.hotkeyChosen = QStringLiteral("kglobalaccel");
            r.gestureSource = QStringLiteral("Plasma global shortcut (%1)")
                                  .arg(settings ? settings->globalShortcut()
                                                : QStringLiteral("unset"));
        }
    }

    r.notifier = QDBusConnection::sessionBus().isConnected()
                     ? QStringLiteral("knotification") : QString();
    r.tray = QtTray::hostSupportsTray() ? trayDisplayName(QStringLiteral("qt"))
                                        : QStringLiteral("none (no tray host)");
    // Sound is the failure nobody can see: the translation finishes, nothing
    // chirps, and "switched off", "no sound theme" and "no player installed"
    // look the same from outside. Same wording as the GNOME shell - the
    // e2e greps this line on both desktops.
    if (settings) {
        SoundPlayer probe(settings);
        const auto st = probe.status(SoundEvent::Finished);
        if (!st.enabled) {
            r.audio = QStringLiteral("off (Sounds.Enabled=false)");
        } else if (!st.problem.isEmpty()) {
            r.audio = st.problem;
        } else {
            r.audio = QStringLiteral("%1 - %2").arg(st.player, st.file);
        }
    }
    // Plasma has no packaged entry (the first-run dialog asks instead), so this
    // line normally names a file in ~/.config - or says there is none, which is
    // the answer a user needs when the app did not come back after a reboot.
    r.autostart = XdgAutostart(desktopBaseName(),
                               QCoreApplication::applicationFilePath())
                      .diagnostic();
    r.reasoning = (settings && settings->disableReasoning())
        ? QStringLiteral("skipped (request options sent)")
        : QStringLiteral("NOT disabled - the model may stream hidden reasoning");
    return r;
}

} // namespace

int main(int argc, char **argv)
{
    QStringList argvList;
    argvList.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        argvList << QString::fromLocal8Bit(argv[i]);
    }
    // Before the app object exists: on a headless box the platform plugin
    // cannot be created at all and Qt aborts, so a CLI mode has to be given
    // offscreen first. Shared with the GNOME shell (app/AppFlavour.cpp) -
    // this rule written per shell is what left KDE aborting on --backend-info
    // while GNOME answered it.
    lt::prepareCliEnvironment(argvList);

    // A mode that draws, on a machine with nothing to draw on, says so in a
    // sentence instead of taking a Qt fatal and a core dump (measured over ssh:
    // `traduku --selftest` gave "Aborted (core dumped)", rc 134).
    if (const QString noDisplay = lt::displayAdviceForMissingDisplay(
            argvList, lt::qtCanPickAPlatform());
        !noDisplay.isEmpty()) {
        qWarning().noquote() << noDisplay;
        return 2;
    }

    QApplication app(argc, argv);
    configureApplication(app, QStringLiteral("kde"), QStringLiteral(LT_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);   // tray-only app

    // One parser for both shells (app/cli.cpp). It used to live in this file
    // and be copied; the copy is what let `--version` print no version and
    // `--backend-info` be unknown here while the GNOME shell answered it.
    const CliOptions cli = parseCommandLine(app, argvList, QStringLiteral("kde"));
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (cli.wantsHelp) {
        out << cliHelpText(app, QStringLiteral("kde")) << '\n';
        return 0;
    }
    if (cli.wantsVersion) {
        out << QStringLiteral("traduku %1 (%2 shell)\n")
                   .arg(QCoreApplication::applicationVersion(),
                        prettyShellName(QStringLiteral("kde")));
        return 0;
    }
    if (cli.exitCode != 0) {
        for (const QString &e : cli.errors) {
            err << "traduku: " << e << '\n';
        }
        err << "try --help\n";
        return cli.exitCode;
    }

    // Settings("") would happily create a file named "", so the default path
    // ($TRADUKU_CONFIG, then XDG) has to come from the other ctor.
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
        // The shared one-shot path (app/cli.cpp): clipboard by session, the
        // missing-tool hint instead of "clipboard is empty", streaming stdout,
        // 0/1. Writing it per shell is what made the two shells disagree about
        // an empty clipboard.
        return runOneShotMode(app, settings, cli);
    case CliOptions::Mode::NotifyTest:
        // Plasma has its own notification server; the KF6 path is exercised by
        // the tray and the controller, so this shell reports rather than tests.
        out << QStringLiteral("notify: knotify (KDE notifications)\n");
        return 0;
    case CliOptions::Mode::Autostart:
        // Same command as the GNOME shell - it drives the object the tray menu
        // drives, so "start at login" has one implementation reached two ways.
        return runAutostartCommand(cli.autostartValue);
    case CliOptions::Mode::Tray:
        break;
    }


    // ---- tray mode: single instance --------------------------------------
    // Requires a session bus (standard on KDE/GNOME; the KF6 service exits a
    // second copy automatically).
    KDBusService service(KDBusService::Unique);

    XdgAutostart autostart(desktopBaseName(),
                           QCoreApplication::applicationFilePath());
    KdeNotifier notifier;
    SoundPlayer sound(&settings);
    auto clipboard = createClipboard(QGuiApplication::platformName(), {},
                                     QApplication::clipboard());
    TranslatorWindow window;
    ChunkedTranslator pipeline(&settings);
    AppController controller({&settings, clipboard.get(), &notifier, &sound,
                              &window, &pipeline});
    // Deliberately never deleted: it is a process-lifetime object, and the
    // KDE app used to crash on exit because the tray sat on the stack here and
    // its destruction ran while the session bus was still alive
    // (tests/unit/tst_trayparity.cpp pins the rule that killed it: a menu item
    // must never tear the application down by itself).
    const TrayChoice trayChoice = resolveTrayKind(
        QLatin1String(LT_TRAY_DEFAULT), qEnvironmentVariable("TRADUKU_TRAY"));
    if (!trayChoice.warning.isEmpty()) {
        qWarning("traduku: %s", qUtf8Printable(trayChoice.warning));
    }
    qInfo("traduku: tray: %s [%s]",
          qUtf8Printable(trayDisplayName(trayChoice.kind)),
          qUtf8Printable(trayChoice.source));
    ITray *tray = createTray(trayChoice.kind, &settings, &autostart);
    // Say which build and which tray this is, in the panel itself: during the
    // Stage 6b comparison a Start-Menu relaunch quietly restarted the
    // *installed* binary and nothing on screen could tell them apart.
    tray->setStatusLabel(QStringLiteral("Traduku %1 · tray %2 [%3]")
                             .arg(QCoreApplication::applicationVersion(),
                                  trayChoice.kind, trayChoice.source));
    tray->show();

    // Hotkeys: passive X11/XWayland monitor first (needs no permission,
    // grabs nothing); Plasma global shortcut as fallback.
    std::unique_ptr<IHotkeyBackend> hotkey;
#ifdef LT_HAVE_X11
    auto x11 = std::make_unique<X11HotkeyBackend>();
    x11->setIntervalMs(qint64(settings.pressInterval() * 1000.0));
    if (x11->start()) {
        hotkey = std::move(x11);
    }
#endif
    ShortcutMonitor *monitor = nullptr;
    if (!hotkey) {
        auto *shortcut = new KdeGlobalShortcut(&app);
        monitor = new ShortcutMonitor(shortcut, &settings, &app);
        monitor->start();
        tray->setToolTipSub(QStringLiteral("Global shortcut: %1 (press twice)")
                               .arg(settings.globalShortcut()));
    }

    controller.setHotkey(monitor ? static_cast<IHotkeyBackend *>(monitor)
                                 : hotkey.get());
    // KF6's KDBusService::Unique exits the second copy for us and emits
    // Activate in the first. Without this connection an icon click is a no-op
    // indistinguishable from a dead app - the GNOME shell has the same pair
    // (SingleInstance::activateRequested), and the message both shells print
    // promises exactly this.
    // activateRequested, not Activate: KF6 made Activate private and this is
    // the public spelling (extra QVariantMap argument is dropped - Qt lets a
    // slot take fewer arguments than the signal carries).
    QObject::connect(&service, &KDBusService::activateRequested, &controller,
                     &AppController::surfaceWindow);
    QObject::connect(tray, &ITray::translateRequested,
                     &controller, &AppController::translateNow);
    QObject::connect(tray, &ITray::quitRequested, &app,
                     [&]() { app.quit(); });   // trays only ask, the shell leaves
    QObject::connect(&controller, &AppController::settingsRequested, &app,
                     [&]() {
                         SettingsDialog dlg(&settings);
                         dlg.exec();
                     });
    QObject::connect(tray, &ITray::settingsRequested, &app, [&]() {
        SettingsDialog dlg(&settings);
        dlg.exec();
    });

    // Follow live settings changes made by the dialog, the tray menu, or
    // an external editor of config.ini (KConfigWatcher equivalent).
    QObject::connect(&settings, &Settings::changed, &app, [&]() {
#ifdef LT_HAVE_X11
        if (auto *x = dynamic_cast<X11HotkeyBackend *>(hotkey.get())) {
            x->setIntervalMs(qint64(settings.pressInterval() * 1000.0));
        }
#endif
    });
    QFileSystemWatcher configWatcher;
    if (QFile::exists(settings.filePath())) {
        configWatcher.addPath(settings.filePath());
    }
    QObject::connect(&configWatcher, &QFileSystemWatcher::fileChanged, &app,
                     [&](const QString &) {
                         settings.reload();
                         if (!QFile::exists(settings.filePath())) {
                             configWatcher.addPath(settings.filePath());
                         }
                     });

    // First run (fresh install / first launch): greet, ask about
    // autostart, open settings - a new user must not need a terminal.
    QTimer::singleShot(0, &app, [&]() {
        if (settings.firstRunDone()) {
            return;
        }
        settings.setFirstRunDone(true);
        settings.save();
        // The decision is shared with the GNOME shell (app/AppFlavour.cpp) and
        // differs on purpose: Plasma has no packaged autostart entry, so this
        // dialog is the only thing that can ask - and the only thing allowed to
        // write one.
        const FirstRunPlan plan = lt::planFirstRun(
            QStringLiteral("kde"), true, !settings.apiUrl().trimmed().isEmpty());
        notifier.notify(QStringLiteral("Traduku is installed"),
                        QStringLiteral("Press Ctrl+C twice quickly to "
                                       "translate the clipboard (a single "
                                       "copy is left alone). Configure your "
                                       "LLM endpoint in the settings dialog."));
        if (!plan.askAboutAutostart) {
            // Not this shell's question to ask (see AppFlavour.h): the entry is
            // installed by the package, and asking about something the user
            // cannot change is how a welcome dialog becomes a lie.
            if (plan.openSettings) {
                SettingsDialog dlg(&settings);
                dlg.exec();
            }
            return;
        }
        const auto answer = QMessageBox::question(
            &window, QStringLiteral("Welcome to Traduku"),
            QStringLiteral("Copy text anywhere, press Ctrl+C twice - the "
                           "translation window opens.\n\n"
                           "Start Traduku automatically at login?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes && !autostart.isEnabled()) {
            autostart.setEnabled(true);
        }
        SettingsDialog dlg(&settings);
        dlg.exec();
    });

    // The "I am up" sentence. The E2E harness waits for this instead of
    // sleeping (tests/e2e/grid.py wait_journal): 4 s per test of blind waiting
    // became an event, and a startup that hangs now fails naming the backend
    // it never got, rather than with an empty screenshot 4 s later.
    lt::ReadyInfo ready;
    ready.shell = QStringLiteral("kde");
    ready.session = sessionType();
    ready.clipboard = clipboard->backendName();
    ready.hotkey = hotkey ? hotkey->backendName() : QString();
    ready.trayAvailable = tray->isAvailable();
    lt::logReady(ready);

    return app.exec();
}
