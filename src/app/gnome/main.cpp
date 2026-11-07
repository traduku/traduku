// traduku: GNOME shell around the platform-agnostic core.
//
// Composition only: settings, backends (clipboard/gesture/notifier/sounds/
// autostart), the AppController and the tray. No business logic here.
//
// Deliberately NO KF6 (GNOME_PLAN.md D3): GNOME ships none of it, and every
// layer this shell needs - tray (SNI + dbusmenu), notifications (FDO),
// global shortcut (gsettings), gesture (/dev/input), clipboard (wl-clipboard)
// - is plain D-Bus/ioctl/exec, so the GNOME build links Qt and nothing else.
//
// Stage status (GNOME_PLAN.md §9): this is the Stage 1 skeleton. The CLI
// modes (--translate, --selftest, --backend-info) are complete; the tray
// mode is refused until the gesture/notifier/tray layers land (Stages 3, 2,
// 5) - refusing loudly beats starting an app that silently never reacts to
// Ctrl+C+C.

#include "app/AppController.h"
#include "app/BackendInfo.h"
#include "app/SelfTest.h"
#include "app/TrayChoice.h"
#include "app/SingleInstance.h"
#include "app/cli.h"
#include "core/chunkedtranslator.h"
#include <QDebug>
#include "core/settings.h"
#include "core/soundplayer.h"
#include "platform/wl/ClipboardFactory.h"
#include "platform/wl/WlClipboard.h"
#include "platform/evdev/EvdevHotkeyBackend.h"
#include "platform/fdo/FdoNotifier.h"
#include "platform/qt/SessionInfo.h"
#include "platform/qt/QtTray.h"
#include "platform/qt/XdgAutostart.h"
#include "ui/SettingsDialog.h"
#include "ui/TranslatorWindow.h"

#include <QApplication>
#include <QDir>
#include <QShortcut>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTextStream>

#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace lt;

namespace {

/// Which QApplication flavour does this invocation need? Widgets require a
/// QApplication, and one cannot be created after a QGuiApplication, so the
/// decision has to be made before the first app object exists (from argv,
/// because QCoreApplication::arguments() needs an instance). Console-only
/// modes must not need a display: `traduku --backend-info` has to answer on
/// a machine where nothing graphical runs yet.
bool cliNeedsWidgets(const QStringList &argv)
{
    static const QStringList cliFlags = {
        QStringLiteral("--translate"), QStringLiteral("-t"),
        QStringLiteral("--selftest"), QStringLiteral("--backend-info"),
        QStringLiteral("--text"), QStringLiteral("--live"),
        QStringLiteral("--notify-test"),
        QStringLiteral("--help"), QStringLiteral("-h"),
        QStringLiteral("--version"),
    };
    for (const QString &a : argv) {
        if (cliFlags.contains(a)) {
            return a == QLatin1String("--selftest");   // only selftest shows a window
        }
    }
    return true;                                       // no mode flag: tray mode
}

/// The gesture stack (GNOME_PLAN.md D1): /dev/input is the primary because
/// it is the only channel that sees the literal Ctrl+C+C on Wayland. The
/// gsettings shortcut fallback lands in the next commit; until then a box
/// without keyboard ACLs has no gesture and the shell refuses to start
/// rather than running deaf.
struct GestureSetup
{
    std::unique_ptr<EvdevHotkeyBackend> evdev;
    IHotkeyBackend *chosen = nullptr;
};

GestureSetup createGesture(Settings &settings, BackendReport &r)
{
    GestureSetup g;
    g.evdev = std::make_unique<EvdevHotkeyBackend>();
    g.evdev->setIntervalMs(qint64(settings.pressInterval() * 1000.0));
    if (g.evdev->start()) {
        g.chosen = g.evdev.get();
        r.hotkeyAttempts.append({QStringLiteral("evdev"),
                                 QStringLiteral("chosen (%1 keyboard(s)")
                                     .arg(g.evdev->openKeyboardCount())
                                 + QStringLiteral(")")});
        r.hotkeyChosen = QStringLiteral("evdev");
        r.gestureSource = QStringLiteral("evdev /dev/input/event*");
        r.inputAccess.clear();
    } else {
        r.hotkeyAttempts.append({QStringLiteral("evdev"), g.evdev->lastError()});
        r.inputAccess = g.evdev->lastError();
    }
    return g;
}

BackendReport collectBackends(Settings *settings)
{
    BackendReport r;
    r.shell = QStringLiteral("gnome");
    // Under a forced offscreen QPA (CI, --selftest on a headless box) the
    // platform name says nothing about the real session, so fall back to
    // XDG_SESSION_TYPE - tests/e2e asserts on this line.
    // Session (not QPA plugin - see platform/qt/SessionInfo.h): a forced
    // offscreen QPA must not make a Wayland box look like it has no Wayland.
    r.sessionType = sessionType();
    if (QGuiApplication::platformName() == QLatin1String("offscreen")
        && !r.sessionType.isEmpty()) {
        r.sessionType = QStringLiteral("%1 (qpa=offscreen)").arg(r.sessionType);
    }
    if (r.sessionType.isEmpty()) {
        r.sessionType = QGuiApplication::platformName();   // last resort
    }
    r.desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");

    // Named without spawning a subprocess: --backend-info must also answer
    // on a machine where the clipboard is unreachable.
    const bool wayland = isWaylandSession();
    if (wayland && WlClipboard::isUsable({}, nullptr)) {
        r.clipboard = QStringLiteral("wl-clipboard");
    } else {
        auto cb = createClipboard(r.sessionType, {}, QGuiApplication::clipboard());
        r.clipboard = cb->backendName();
        r.clipboardHint = cb->diagnostic();
    }

    // gesture/notifier/tray: the gesture is probed for real (start/stop),
    // which is the only way --backend-info can tell "works" from "no ACL".
    Settings probeSettings;   // interval only; --backend-info must not need a config
    GestureSetup gesture = createGesture(probeSettings, r);
    if (gesture.chosen) {
        gesture.evdev->stop();
    }
    // The FDO service is what gnome-shell actually shows; no KF6, no
    // libnotify, one D-Bus call.
    r.notifier = FdoNotifier::isAvailable() ? QStringLiteral("fdo") : QString();
    // --backend-info may be called without a config (collectBackends(nullptr)
    // is legal), so nothing here may assume settings exists.
    r.reasoning = settings && settings->disableReasoning()
        ? QStringLiteral("skipped (request options sent)")
        : QStringLiteral("NOT disabled - the model may stream hidden reasoning");
    // GNOME 50 + the appindicator extension gives an SNI watcher, and Qt's
    // QSystemTrayIcon is Qt's own generic SNI client there (D3). Without a
    // watcher the app still works - so report, do not fail.
    r.tray = QtTray::hostSupportsTray() ? trayDisplayName(QStringLiteral("qt"))
                                       : trayDisplayName(QStringLiteral("none"));
    return r;
}

int doTranslate(QCoreApplication &app, Settings &settings, const CliOptions &cli)
{
    QTextStream out(stdout);
    // Chosen by session: under a forced offscreen QPA the Qt clipboard
    // reads nothing on Wayland, while wl-paste works.
    auto clipboard = createClipboard(sessionType(), {}, QGuiApplication::clipboard());
    const QString text =
        (cli.text.isNull() ? clipboard->text() : cli.text).trimmed();
    if (text.isEmpty()) {
        // The backend knows when it is structurally blind (missing
        // wl-clipboard on Wayland): say that instead of blaming the copy.
        const QString hint = clipboard->diagnostic();
        QTextStream(stderr) << (hint.isEmpty()
                                    ? QStringLiteral("traduku: nothing to translate "
                                                     "(clipboard is empty).\n")
                                    : hint + QLatin1Char('\n'));
        return 1;
    }
    int rc = 1;
    runOneShotTranslation(&settings, text,
                          [&out](const QString &chunk) {
                              out << chunk;
                              out.flush();          // stream into the pipe
                          },
                          [&rc](int code, const QString &error) {
                              rc = code;
                              if (code == 0) {
                                  QTextStream(stdout) << '\n';
                              } else {
                                  QTextStream(stderr) << "traduku: " << error << '\n';
                              }
                              qApp->quit();
                          });
    app.exec();
    return rc;
}

int doNotifyTest()
{
    QTextStream out(stdout);
    FdoNotifier notifier;
    if (!FdoNotifier::isAvailable()) {
        out << "notification service: absent - " << notifier.errorText() << '\n';
        out << "       (gnome-shell owns org.freedesktop.Notifications; "
               "nothing is listening here)\n";
        return 1;
    }
    notifier.notify(QStringLiteral("Traduku"),
                    QStringLiteral("If you can read this, GNOME notifications "
                                   "work. (traduku --notify-test)"));
    // The call is fire-and-forget; wait (bounded) for the daemon's reply so
    // the exit code means something.
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 3000 && notifier.lastId() == 0
           && notifier.errorText().isEmpty()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (notifier.lastId() != 0) {
        out << "notification service: accepted (id " << notifier.lastId() << ")\n";
        return 0;
    }
    out << "notification service: rejected - "
        << (notifier.errorText().isEmpty() ? QStringLiteral("no reply within 3 s")
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

    std::unique_ptr<QCoreApplication> app(
        cliNeedsWidgets(argvList)
            ? static_cast<QCoreApplication *>(new QApplication(argc, argv))
            : static_cast<QCoreApplication *>(new QGuiApplication(argc, argv)));
    if (auto *widgetApp = qobject_cast<QApplication *>(app.get())) {
        widgetApp->setQuitOnLastWindowClosed(false);   // tray-only app
    }
    configureApplication(*app, QStringLiteral("gnome"), QStringLiteral(LT_VERSION));

    const CliOptions cli = parseCommandLine(*app, argvList, QStringLiteral("gnome"));
    QTextStream out(stdout), err(stderr);

    if (cli.wantsHelp) {
        out << cliHelpText(*app, QStringLiteral("gnome")) << '\n';
        return 0;
    }
    if (cli.wantsVersion) {
        out << QStringLiteral("traduku %1 (%2 shell)\n")
                   .arg(QCoreApplication::applicationVersion(),
                        prettyShellName(QStringLiteral("gnome")));
        return 0;
    }
    if (cli.exitCode != 0) {
        for (const QString &e : cli.errors) {
            err << "traduku: " << e << '\n';
        }
        err << "try --help\n";
        return cli.exitCode;
    }

    // Settings("") would happily create a file named "", so the default
    // path ($TRADUKU_CONFIG, then XDG) has to come from the other ctor.
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
        return doTranslate(*app, settings, cli);
    case CliOptions::Mode::NotifyTest:
        return doNotifyTest();
    case CliOptions::Mode::Tray:
        break;
    }

    // ---- tray mode ------------------------------------------------------
    // One Traduku per session: a second launch wakes the running one instead
    // of starting a duplicate tray (GNOME_PLAN.md D5).
    SingleInstance instance;
    if (!instance.start()) {
        out << QStringLiteral("Traduku is already running (asked it to surface "
                              "its window).\n");
        return 0;
    }

    // A tray app that silently never reacts to Ctrl+C+C is worse than a
    // refusal, so a working gesture backend is a hard requirement.
    BackendReport report;
    GestureSetup gesture = createGesture(settings, report);
    if (!gesture.chosen) {
        report.shell = QStringLiteral("gnome");
        err << "traduku: no gesture backend available - Ctrl+C+C would do "
               "nothing.\n";
        err << formatBackendInfo(report);
        return 5;
    }

    // The composition root is DE-agnostic: build the backends, hand them
    // over, run. No tray icon yet (Stage 5) - running tray-less is a
    // requirement, not a fallback: GNOME shows an SNI only with an
    // extension installed.
    FdoNotifier notifier;
    SoundPlayer sound(&settings);
    auto clipboard = createClipboard(sessionType(), {}, QGuiApplication::clipboard());
    TranslatorWindow window;
    ChunkedTranslator pipeline(&settings);
    AppController controller({&settings, clipboard.get(), &notifier, &sound,
                              &window, &pipeline});
    controller.setHotkey(gesture.chosen);

    auto openSettings = [&settings]() {
        SettingsDialog dlg(&settings);
        dlg.exec();
    };
    QObject::connect(&controller, &AppController::settingsRequested, app.get(), openSettings);

    // ---- tray ------------------------------------------------------------
    // QSystemTrayIcon, which on Wayland is Qt's generic DBus SNI client
    // (measured on this desktop; tools/spike/tray-spike.cpp compares it with
    // our own SNI client). Its menu reproduces what the KDE tray used to
    // offer - left click translates, right click is the menu - and the KDE
    // implementation is now deleted, so this is the only tray in the tree -
    // verified in a live Plasma session and in GNOME's appindicator host.
    XdgAutostart autostart(QStringLiteral("dev.traduku.Traduku"),
                           QCoreApplication::applicationFilePath());
    QtTray tray(&settings, &autostart);
    QObject::connect(&tray, &QtTray::translateRequested, &controller,
                     &AppController::translateNow);
    QObject::connect(&tray, &QtTray::settingsRequested, app.get(), openSettings);
    QObject::connect(&tray, &QtTray::quitRequested, app.get(), &QCoreApplication::quit);
    if (gesture.chosen) {
        tray.setToolTipSub(QStringLiteral("Press Ctrl+C twice to translate the clipboard"));
    }
    tray.setStatusLabel(QStringLiteral("Traduku %1 · tray qt")
                            .arg(QCoreApplication::applicationVersion()));
    tray.show();

    // No tray icon yet (Stage 5), so stderr is the only way to answer the
    // one question that matters when "nothing happens": did the gesture fire
    // at all, and did the translation come back?
    QObject::connect(gesture.chosen, &IHotkeyBackend::activated, app.get(),
                     []() {
                         qInfo("traduku: gesture detected, translating the clipboard");
                     });
    QObject::connect(&pipeline, &ChunkedTranslator::startedDocument, app.get(),
                     [&tray](const QString &source, const QString &target) {
                         qInfo("traduku: translating %d chars -> %s",
                               source.size(), qUtf8Printable(target));
                         tray.setBusy(true);
                     });
    QObject::connect(&pipeline, &ChunkedTranslator::allDone, app.get(),
                     [&tray]() {
                         qInfo("traduku: translation ready");
                         tray.setBusy(false);
                     });
    QObject::connect(&pipeline, &ChunkedTranslator::failed, app.get(),
                     [&tray](const QString &reason) {
                         qInfo("traduku: translation failed: %s", qUtf8Printable(reason));
                         tray.setBusy(false);
                     });

    // No tray icon yet (Stage 5), so the window needs a way out.
    auto *quit = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), &window);
    QObject::connect(quit, &QShortcut::activated, app.get(), []() { qApp->quit(); });

    out << QStringLiteral("Traduku is running (GNOME, %1 session): press "
                          "Ctrl+C twice to translate. Ctrl+Q quits.\n")
               .arg(sessionType());
    if (!tray.isAvailable()) {
        // Not an error: a GNOME without an SNI extension has no tray, and the
        // app is fully usable without one (gesture, window, notifications).
        out << QStringLiteral("traduku: no tray host on this session - running "
                              "tray-less. Install the AppIndicator/KStatusNotifierItem "
                              "shell extension for a panel icon.\n");
    }
    out.flush();          // a SIGINT kill would otherwise eat the banner
    return app->exec();
}
