// traduku: KDE Plasma shell around the platform-agnostic core.
//
// Composition only: settings, backends (clipboard/hotkey/notifier/sounds/
// autostart), the AppController and the tray. No business logic here.

#include "app/AppController.h"
#include "app/SelfTest.h"
#include "app/TrayChoice.h"
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
#include "platform/qt/XdgAutostart.h"
#include "platform/wl/ClipboardFactory.h"
#include "ui/SettingsDialog.h"
#include "ui/TranslatorWindow.h"

#include <KDBusService>

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

/// The tray the shell shows is configuration, not code (GNOME_PLAN.md D3 and
/// A tray that is not there: LT_TRAY=none / TRADUKU_TRAY=none (kiosk images,
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

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("traduku"));
    QApplication::setApplicationDisplayName(QStringLiteral("Traduku"));
    // Without this, --version printed a name and no number - half of "which
    // build is in my panel?" (the GNOME shell has set it since Stage 1).
    QApplication::setApplicationVersion(QStringLiteral(LT_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);   // tray-only app

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Translate the clipboard with an OpenAI-compatible LLM "
                       "(streaming). Double Ctrl+C in a session, or "
                       "--translate for a one-shot from the shell."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption translateOpt(
        {QStringLiteral("t"), QStringLiteral("translate")},
        QStringLiteral("Translate the clipboard (or --text) with the "
                       "configured LLM, stream the result to stdout, exit."));
    parser.addOption(translateOpt);
    const QCommandLineOption textOpt(
        QStringLiteral("text"),
        QStringLiteral("Text to translate (default: clipboard)."),
        QStringLiteral("text"));
    parser.addOption(textOpt);
    const QCommandLineOption selftestOpt(
        QStringLiteral("selftest"),
        QStringLiteral("Run the built-in smoke test (headless-safe) and exit. "
                       "Add --live to also translate once via the configured "
                       "endpoint."));
    parser.addOption(selftestOpt);
    const QCommandLineOption liveOpt(
        QStringLiteral("live"),
        QStringLiteral("With --selftest: include a live request to the "
                       "configured API."));
    parser.addOption(liveOpt);
    parser.process(app);

    if (parser.isSet(selftestOpt)) {
        QTextStream out(stdout);
        return lt::runSelfTest(out, parser.isSet(liveOpt)) == 0 ? 0 : 1;
    }

    Settings settings;

    // ---- one-shot CLI mode (no tray, no single-instance) ----------------
    if (parser.isSet(translateOpt)) {
        auto cliClipboard = createClipboard(
            QGuiApplication::platformName(), {}, QApplication::clipboard());
        QString text = parser.isSet(textOpt)
                           ? parser.value(textOpt)
                           : cliClipboard->text();
        text = text.trimmed();
        if (text.isEmpty()) {
            qWarning("Clipboard is empty, nothing to translate.");
            return 1;
        }
        TranslationEngine engine(&settings);
        QObject::connect(&engine, &TranslationEngine::delta,
                         [](const QString &chunk) {
                             // The temporary QTextStream flushes on destruction,
                             // so each delta hits stdout as it arrives.
                             QTextStream(stdout) << chunk;
                         });
        int rc = 1;
        QObject::connect(&engine, &TranslationEngine::finished,
                         [&rc](const QString &) {
                             QTextStream(stdout) << '\n';
                             rc = 0;
                             qApp->quit();
                         });
        QObject::connect(&engine, &TranslationEngine::failed,
                         [&rc](const QString &e) {
                             qWarning("%s", qUtf8Printable(e));
                             rc = 1;
                             qApp->quit();
                         });
        engine.translate(text);
        return app.exec() ? 1 : rc;
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
