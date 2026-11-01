// kde-llm-translate: KDE Plasma shell around the platform-agnostic core.
//
// Composition only: settings, backends (clipboard/hotkey/notifier/sounds/
// autostart), the AppController and the tray. No business logic here.

#include "app/AppController.h"
#include "app/SelfTest.h"
#include "core/llmstreamclient.h"
#include "core/settings.h"
#include "core/shortcutmonitor.h"
#include "core/soundplayer.h"
#include "core/chunkedtranslator.h"
#include "core/translationengine.h"
#include "platform/kde/KdeGlobalShortcut.h"
#include "platform/kde/KdeNotifier.h"
#include "platform/kde/KdeTray.h"
#include "platform/qt/QtClipboard.h"
#include "platform/qt/XdgAutostart.h"
#include "platform/wl/ClipboardFactory.h"
#include "ui/SettingsDialog.h"
#include "ui/TranslatorWindow.h"

#include <KDBusService>

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileSystemWatcher>
#include <QTextStream>
#include <QTimer>
#include <memory>

// X11 headers clash with Qt's (Status/None/True/False): include LAST.
#ifdef LT_HAVE_X11
#include "platform/x11/X11HotkeyBackend.h"
#endif

using namespace lt;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("llm-translate"));
    QApplication::setApplicationDisplayName(QStringLiteral("LLM Translate"));
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

    XdgAutostart autostart(QStringLiteral("llm-translate"),
                           QCoreApplication::applicationFilePath());
    KdeNotifier notifier;
    SoundPlayer sound(&settings);
    auto clipboard = createClipboard(QGuiApplication::platformName(), {},
                                     QApplication::clipboard());
    TranslatorWindow window;
    ChunkedTranslator pipeline(&settings);
    AppController controller({&settings, clipboard.get(), &notifier, &sound,
                              &window, &pipeline});
    KdeTray tray(&settings, &autostart);

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
        tray.setToolTipSub(QStringLiteral("Global shortcut: %1 (press twice)")
                               .arg(settings.globalShortcut()));
    }

    controller.setHotkey(monitor ? static_cast<IHotkeyBackend *>(monitor)
                                 : hotkey.get());
    QObject::connect(&tray, &KdeTray::translateRequested,
                     &controller, &AppController::translateNow);
    QObject::connect(&controller, &AppController::settingsRequested, &app,
                     [&]() {
                         SettingsDialog dlg(&settings);
                         dlg.exec();
                     });
    QObject::connect(&tray, &KdeTray::settingsRequested, &app, [&]() {
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

    // Window -> hotkey interval follow + first-run welcome.
    QTimer::singleShot(0, &app, [&]() {
        if (!settings.firstRunDone()) {
            settings.setFirstRunDone(true);
            settings.save();
            notifier.notify(QStringLiteral("LLM Translate"),
                            QStringLiteral("Press Ctrl+C twice quickly to "
                                           "translate the clipboard (a single "
                                           "copy is left alone). Set your LLM "
                                           "endpoint in Settings\u2026"));
        }
    });

    return app.exec();
}
