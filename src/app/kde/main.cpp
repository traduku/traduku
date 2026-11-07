// traduku: KDE Plasma shell around the platform-agnostic core.
//
// Composition only: settings, backends (clipboard/hotkey/notifier/sounds/
// autostart), the AppController and the tray. No business logic here.

#include "app/AppController.h"
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

/// The tray the shell shows is configuration, not code (GNOME_PLAN.md D3).
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
    r.desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");

    auto cb = createClipboard(r.sessionType, {}, QGuiApplication::clipboard());
    r.clipboard = cb->backendName();
    r.clipboardHint = cb->diagnostic();

#ifdef LT_HAVE_X11
    // Conditions, not a probe: starting the X listener would spawn a child
    // process just to answer a question.
    if (qEnvironmentVariableIsEmpty("DISPLAY")) {
        r.hotkeyAttempts.append({QStringLiteral("x11/xrecord"),
                                 QStringLiteral("skipped: no DISPLAY")});
    } else if (X11HotkeyBackend::helperPath().isEmpty()) {
        r.hotkeyAttempts.append({QStringLiteral("x11/xrecord"),
                                 QStringLiteral("unavailable: traduku-record-keys "
                                                "is not installed next to the app "
                                                "(traduku-gnome does not ship it)")});
    } else {
        r.hotkeyAttempts.append({QStringLiteral("x11/xrecord"),
                                 QStringLiteral("chosen")});
        r.hotkeyChosen = QStringLiteral("x11/xrecord (helper: %1)")
                             .arg(X11HotkeyBackend::helperPath());
        r.gestureSource = QStringLiteral("X RECORD on %1")
                              .arg(qEnvironmentVariable("DISPLAY"));
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
    r.reasoning = (settings && settings->disableReasoning())
        ? QStringLiteral("skipped (request options sent)")
        : QStringLiteral("NOT disabled - the model may stream hidden reasoning");
    return r;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    configureApplication(app, QStringLiteral("kde"), QStringLiteral(LT_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("Traduku"));
    QApplication::setQuitOnLastWindowClosed(false);   // tray-only app

    // One parser for both shells (app/cli.cpp). It used to live in this file
    // and be copied; the copy is what let `--version` print no version and
    // `--backend-info` be unknown here while the GNOME shell answered it.
    const CliOptions cli = parseCommandLine(app, QApplication::arguments(),
                                            QStringLiteral("kde"));
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
    case CliOptions::Mode::Tray:
        break;
    }


    // ---- tray mode: single instance --------------------------------------
    // Requires a session bus (standard on KDE/GNOME; the KF6 service exits a
    // second copy automatically).
    KDBusService service(KDBusService::Unique);

    XdgAutostart autostart(QStringLiteral("traduku"),
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
        notifier.notify(QStringLiteral("Traduku is installed"),
                        QStringLiteral("Press Ctrl+C twice quickly to "
                                       "translate the clipboard (a single "
                                       "copy is left alone). Configure your "
                                       "LLM endpoint in the settings dialog."));
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

    return app.exec();
}
