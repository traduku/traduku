// traduku: GNOME shell around the platform-agnostic core.
//
// Composition only: settings, backends (clipboard/gesture/notifier/sounds/
// autostart), the AppController and the tray. No business logic here.
//
// Deliberately NO KF6 (ROADMAP.md §2): GNOME ships none of it, and every
// layer this shell needs - tray (SNI + dbusmenu), notifications (FDO),
// global shortcut (gsettings), gesture (/dev/input), clipboard (wl-clipboard)
// - is plain D-Bus/ioctl/exec, so the GNOME build links Qt and nothing else.
//
// Stage status (ROADMAP.md §3): this is the Stage 1 skeleton. The CLI
// modes (--translate, --selftest, --backend-info) are complete; the tray
// mode is refused until the gesture/notifier/tray layers land (Stages 3, 2,
// 5) - refusing loudly beats starting an app that silently never reacts to
// Ctrl+C+C.

#include "app/AppController.h"
#include "app/AppFlavour.h"
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

#include <QTimer>
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

/// Which QApplication flavour: widgets need QApplication, and one cannot be
/// created after a QGuiApplication, so the choice is made from argv before the
/// first app object exists. The rule itself is shared with the KDE shell -
/// app/AppFlavour.cpp - because two copies of it is how KDE ended up with no
/// headless path at all.
/// The gesture stack (ROADMAP.md §2): /dev/input is the primary because
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
    if (qpaWasForcedByUs()) {
        // Same wording as the KDE shell: "offscreen" alone reads as "no
        // session", when what is missing is our knowledge of it.
        r.sessionType = QStringLiteral("%1 (no display here)").arg(r.sessionType);
    }
    r.desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");

    // One decision for both questions: what the report says and what the app
    // does must come from the same call. This used to hardcode "wl-clipboard"
    // here whenever the tools were present, which meant the report kept
    // saying wl-clipboard after the user set TRADUKU_CLIPBOARD=x11 - the knob
    // and the diagnostic disagreed, and the user could not tell which one was
    // lying. The factory already answers without blocking: it only constructs
    // a reader after the probe said it works.
    auto cb = createClipboard(r.sessionType, {}, QGuiApplication::clipboard());
    r.clipboard = cb->backendName();
    r.clipboardHint = cb->diagnostic();
    if (r.clipboardHint.isEmpty()) {
        // A forced TRADUKU_CLIPBOARD that did not take effect is the one case
        // where the chosen backend has nothing to complain about and the user
        // still has a question.
        r.clipboardHint = forcedBackendProblem(r.clipboard);
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
    // Sound is the failure nobody can see: the translation finishes, nothing
    // chirps, and "switched off", "no sound theme" and "no player installed"
    // look identical from the outside. Naming the player that would run is the
    // point - on ubuntu-gnome that is pw-play alone (measured: no paplay, no
    // aplay), which is exactly the answer a guess would get wrong.
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
    // "Does it come back at login?" is answered by a file the package owns on
    // this desktop and by the user on Plasma; the report says which.
    r.autostart = XdgAutostart(QStringLiteral("traduku"),
                               QCoreApplication::applicationFilePath())
                      .diagnostic();
    return r;
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

    lt::prepareCliEnvironment(argvList);

    std::unique_ptr<QCoreApplication> app(
        lt::needsWidgets(argvList)
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
        return runOneShotMode(*app, settings, cli);
    case CliOptions::Mode::NotifyTest:
        return doNotifyTest();
    case CliOptions::Mode::Autostart:
        return runAutostartCommand(cli.autostartValue);
    case CliOptions::Mode::Tray:
        break;
    }

    // ---- tray mode ------------------------------------------------------
    // One Traduku per session: a second launch wakes the running one instead
    // of starting a duplicate tray (ROADMAP.md §2).
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
    // The id IS the file name, and it has to be the name the package uses
    // (/etc/xdg/autostart/traduku.desktop): XDG shadowing works by file name,
    // so an entry called dev.traduku.Traduku could never disable the packaged
    // one - "Launch on Login" then moved a file that was not the reason the
    // app started. dev.traduku.Traduku stays the D-Bus / single-instance name,
    // a different namespace that shadows nothing.
    XdgAutostart autostart(QStringLiteral("traduku"),
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

    // ---- first run -----------------------------------------------------
    // A program whose only interface is a key gesture is invisible until
    // somebody presses it, and on GNOME there is usually no icon either (the
    // panel shows one only with the appindicator extension). The greeting is
    // the one moment that can be spent; what it may not do is ask about
    // autostart - this shell installs /etc/xdg/autostart/traduku.desktop in
    // the package, so the question has an answer the user cannot change.
    QTimer::singleShot(0, app.get(), [&]() {
        if (settings.firstRunDone()) {
            return;
        }
        settings.setFirstRunDone(true);
        settings.save();
        const FirstRunPlan plan = lt::planFirstRun(
            QStringLiteral("gnome"), tray.isAvailable(),
            !settings.apiUrl().trimmed().isEmpty());
        if (!plan.greet) {
            return;
        }
        QString body = QStringLiteral(
            "Press Ctrl+C twice quickly to translate the clipboard (a single "
            "copy is left alone).");
        if (plan.explainMissingIcon) {
            // "Where is the icon?" is the first question on a stock GNOME, and
            // the answer is an extension GNOME does not ship.
            body += QStringLiteral(
                "\n\nNo icon in the top bar? GNOME only shows one with the "
                "\u201cAppIndicator and KStatusNotifierItem Support\u201d "
                "extension (GNOME Extensions, or gnome-shell-extension-appindicator).");
        }
        if (plan.openSettings) {
            body += QStringLiteral(" Set your LLM endpoint in the window that "
                                  "just opened.");
        }
        notifier.notify(QStringLiteral("Traduku is installed"), body);
        if (plan.openSettings) {
            // Only with nothing to translate with: this app autostarts
            // unattended at login, and a modal at login is how people learn to
            // switch autostart off.
            SettingsDialog dlg(&settings);
            dlg.exec();
        }
    });

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
