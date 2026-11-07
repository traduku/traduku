#include "cli.h"

#include "core/settings.h"
#include "core/translationengine.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

#include <functional>
#include <memory>

namespace lt {

QString prettyShellName(const QString &shell)
{
    return shell == QLatin1String("gnome") ? QStringLiteral("GNOME")
                                           : QStringLiteral("KDE Plasma");
}

namespace {

void addOptions(QCommandLineParser &parser, const QString &shell)
{
    parser.setApplicationDescription(
        QStringLiteral("Translate the clipboard with an OpenAI-compatible LLM "
                       "(streaming). %1 shell: press Ctrl+C twice to "
                       "translate, or use --translate for a one-shot from "
                       "the shell.")
            .arg(prettyShellName(shell)));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOptions({
        {{QStringLiteral("t"), QStringLiteral("translate")},
         QStringLiteral("Translate the clipboard (or --text) with the "
                        "configured LLM, stream the result to stdout, exit.")},
        {QStringLiteral("text"),
         QStringLiteral("Text to translate (default: the clipboard). "
                        "Requires --translate."),
         QStringLiteral("text")},
        {QStringLiteral("config"),
         QStringLiteral("Read settings from this file instead of "
                        "$TRADUKU_CONFIG / ~/.config/traduku/config.ini "
                        "(one instance against another endpoint, tests)."),
         QStringLiteral("path")},
        {QStringLiteral("selftest"),
         QStringLiteral("Run the built-in smoke test (headless-safe) and "
                        "exit. Add --live to also translate once via the "
                        "configured endpoint.")},
        {QStringLiteral("live"),
         QStringLiteral("With --selftest: include a live request to the "
                        "configured API.")},
        {QStringLiteral("notify-test"),
         QStringLiteral("Send one test notification through the desktop "
                        "service and report whether it was accepted. "
                        "Support-friendly: no clipboard, no model call.")},
        {QStringLiteral("backend-info"),
         QStringLiteral("Print which backend was chosen for clipboard, "
                        "gesture, notifications, tray and audio - and what "
                        "to fix when one of them is unusable. Never touches "
                        "the clipboard.")},
    });
}

} // namespace

void configureApplication(QCoreApplication &app, const QString &shell,
                          const QString &version)
{
    Q_UNUSED(app);
    // setApplicationVersion was the one nobody ever called, which made
    // `traduku --version` print a name and no number.
    QCoreApplication::setApplicationName(QStringLiteral("traduku"));
    QCoreApplication::setApplicationVersion(version);
}

CliOptions parseCommandLine(QCoreApplication &app, const QStringList &args,
                            const QString &shell)
{
    Q_UNUSED(app);
    QCommandLineParser parser;
    addOptions(parser, shell);

    CliOptions o;
    if (!parser.parse(args)) {
        o.errors << parser.errorText();
        o.exitCode = 2;
        return o;
    }

    // help/version are flags, not exit() calls: --backend-info and the
    // tests must be able to look at the result.
    o.wantsHelp = parser.isSet(QStringLiteral("help"));
    o.wantsVersion = parser.isSet(QStringLiteral("version"));
    if (o.wantsHelp || o.wantsVersion) {
        return o;
    }

    const bool translate = parser.isSet(QStringLiteral("translate"));
    const bool selftest = parser.isSet(QStringLiteral("selftest"));
    const bool info = parser.isSet(QStringLiteral("backend-info"));
    const bool notifyTest = parser.isSet(QStringLiteral("notify-test"));
    o.live = parser.isSet(QStringLiteral("live"));
    if (parser.isSet(QStringLiteral("config"))) {
        o.configPath = parser.value(QStringLiteral("config"));
    }

    if (int(translate) + int(selftest) + int(info) + int(notifyTest) > 1) {
        o.errors << QStringLiteral(
            "only one of --translate, --selftest, --backend-info, "
            "--notify-test can be used at a time");
        o.exitCode = 2;
        return o;
    }
    if (translate) {
        o.mode = CliOptions::Mode::Translate;
    } else if (selftest) {
        o.mode = CliOptions::Mode::SelfTest;
    } else if (info) {
        o.mode = CliOptions::Mode::BackendInfo;
    } else if (notifyTest) {
        o.mode = CliOptions::Mode::NotifyTest;
    }

    if (parser.isSet(QStringLiteral("text"))) {
        o.text = parser.value(QStringLiteral("text"));
        if (!translate) {
            // `traduku --text foo` would otherwise start the tray silently
            // and look like the option had been ignored.
            o.errors << QStringLiteral("--text needs --translate");
            o.exitCode = 2;
            return o;
        }
    }
    if (o.live && !selftest) {
        o.errors << QStringLiteral("--live only makes sense together with --selftest");
        o.exitCode = 2;
        return o;
    }
    return o;
}

QString cliHelpText(QCoreApplication &app, const QString &shell)
{
    Q_UNUSED(app);
    QCommandLineParser parser;
    addOptions(parser, shell);
    return parser.helpText();
}

void runOneShotTranslation(Settings *settings, const QString &text,
                           std::function<void(const QString &)> onDelta,
                           std::function<void(int, const QString &)> onDone)
{
    if (!onDone) {
        return;                     // nothing to report to - do not start
    }
    if (text.trimmed().isEmpty()) {
        onDone(1, QStringLiteral("nothing to translate (text/clipboard is empty)"));
        return;
    }

    // Heap-allocated and self-destructing: the caller's event loop owns it,
    // and a `finished` followed by `failed` must not report twice.
    auto *engine = new TranslationEngine(settings);
    auto reported = std::make_shared<bool>(false);
    QObject::connect(engine, &TranslationEngine::delta, engine,
                     [onDelta](const QString &chunk) {
                         if (onDelta) {
                             onDelta(chunk);       // streaming display is optional
                         }
                     });
    QObject::connect(engine, &TranslationEngine::finished, engine,
                     [reported, onDone, engine](const QString &) {
                         if (!*reported) {
                             *reported = true;
                             onDone(0, QString());
                         }
                         engine->deleteLater();
                     });
    QObject::connect(engine, &TranslationEngine::failed, engine,
                     [reported, onDone, engine](const QString &error) {
                         if (!*reported) {
                             *reported = true;
                             onDone(1, error);
                         }
                         engine->deleteLater();
                     });
    engine->translate(text.trimmed());
}

} // namespace lt
