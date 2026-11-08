#include "SelfTest.h"

#include "core/doublepressdetector.h"
#include "core/languages.h"
#include "core/settings.h"
#include "core/sseparser.h"
#include "core/unicodeescapes.h"
#include "core/texttargets.h"
#include "core/aligner.h"
#include "core/chunker.h"
#include "core/translationengine.h"
#include "core/translationledger.h"
#include "app/PlatformBackends.h"
#include "ui/TranslatorWindow.h"

#include <QCoreApplication>
#include <QLabel>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTextEdit>

#include <functional>

namespace lt {

namespace {
struct Runner
{
    QTextStream &out;
    int failures = 0;
    void check(bool ok, const char *what)
    {
        out << (ok ? "PASS  " : "FAIL  ") << what << '\n';
        if (!ok) {
            ++failures;
        }
    }
    /// A row that does not apply on this machine is said so, not deleted.
    /// `--selftest` doubles as the document a user reads to learn what was
    /// checked, and a line that is simply absent is indistinguishable from a
    /// line that was never written (ROADMAP.md §4.5b: a skip carries its
    /// reason, it does not carry silence).
    void skip(const QString &what, const QString &why)
    {
        out << "SKIP  " << what << " - " << why << '\n';
    }
};
} // namespace

int runSelfTest(QTextStream &out, bool doLive, const QString &configPath)
{
    Runner r{out, 0};

    // 1. Settings: defaults + persistence
    {
        QTemporaryDir dir;
        Settings s(dir.path() + QStringLiteral("/config.ini"));
        r.check(s.apiUrl() == QLatin1String("http://localhost:11434/v1")
                    && s.pressInterval() == 0.5 && s.copyResult(),
                "settings: defaults");
        s.setTargetLanguage(QStringLiteral("fr"));
        s.setModel(QStringLiteral("selftest-model"));
        s.save();
        Settings s2(dir.path() + QStringLiteral("/config.ini"));
        r.check(s2.targetLanguage() == QLatin1String("fr")
                    && s2.model() == QLatin1String("selftest-model"),
                "settings: save/reload round-trip");
    }

    // 2. Languages
    r.check(languages().size() >= 30 && languageName(QStringLiteral("ru"))
                .contains(QStringLiteral("Russian"))
                && languageName(QStringLiteral("zz")).isEmpty(),
            "languages: table + lookup");

    {
        const QString doc = QStringLiteral("alpha\nbeta\n\ngamma");
        const auto chunks = splitChunks(doc, 60);
        QString joined;
        for (const Chunk &c : chunks) {
            joined += c.text + c.sep;
        }
        r.check(joined == doc && chunks.size() == 3,
                "chunker: split preserves the document");
        TranslationLedger led;
        led.setSource(doc);
        led.setTranslation(0, QStringLiteral("ALPHA"), QString());
        led.setTranslation(1, QStringLiteral("BETA"), QString());
        led.setTranslation(2, QStringLiteral("GAMMA"), QString());
        QVector<int> dirty = led.applyEdit(QStringLiteral("alpha\nbeta2\n\ngamma"));
        r.check(dirty.size() == 1 && dirty.at(0) == 1
                    && led.targetText() == QStringLiteral("ALPHA\nbeta2\n\nGAMMA"),
                "ledger: edit keeps untouched translations");
        const auto ac = alignChunks(splitChunks(QStringLiteral("Hello world.\nBye now.")),
                                    splitChunks(QStringLiteral("Hallo Welt.\nTschues.")));
        r.check(ac.size() == 2 && ac.at(0) == QLatin1String("Hallo Welt."),
                "aligner: whole-text result maps back to chunks");
    }


    // 3. Double Ctrl+C detection (fake clock, no X server)
    {
        DoublePressDetector d;
        qint64 now = 0;
        d.setClock([&now]() { return now; });
        d.setIntervalMs(500);
        int fires = 0;
        QObject::connect(&d, &DoublePressDetector::activated,
                         [&fires]() { ++fires; });
        auto chord = [&]() {
            now += 50;
            d.keyEvent(DoublePressDetector::Press, KeyCtrlL, 0);
            d.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
            d.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
            d.keyEvent(DoublePressDetector::Release, KeyCtrlL, 0);
        };
        chord();
        r.check(fires == 0, "hotkey: single Ctrl+C does not trigger");
        chord();
        r.check(fires == 1, "hotkey: double Ctrl+C triggers once");
        for (int i = 0; i < 30; ++i) {
            now += 10;
            d.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
            d.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        }
        r.check(fires == 1, "hotkey: typing c without Ctrl never triggers");
    }

    // 4. SSE streaming parse (byte fragments)
    {
        SseParser p;
        const QByteArray full =
            "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n"
            "data: [DONE]\n\n";
        for (int i = 0; i < full.size(); i += 3) {
            p.append(full.mid(i, 3));
        }
        QStringList deltas;
        for (const QString &d : p.takeDeltas()) {
            deltas << d;
        }
        r.check(deltas.join(QString()) == QLatin1String("ok") && p.isDone(),
                "sse: fragmented streaming parse");
    }

    // 5. Prompt
    {
        const QString p = TranslationEngine::buildPrompt(
            QStringLiteral("abc"), QStringLiteral("Deutsch (German)"));
        r.check(p.contains(QStringLiteral("<<<TEXT\nabc\nTEXT"))
                    && p.contains(QStringLiteral("Deutsch (German)")),
                "prompt: markers + language");
    }

    // 6. Result window streaming (offscreen)
    {
        TranslatorWindow w;
        w.statusTranslating(QStringLiteral("ru"));
        w.showTargetRows({QStringLiteral("ab"), QStringLiteral("mid"),
                          QStringLiteral("cd")});
        w.streamIntoRow(1, QStringLiteral("B"));
        w.commitRow(1, QStringLiteral("B"));
        w.statusDone();
        auto *tgt = w.findChild<QTextEdit *>(QStringLiteral("m_target"));
        auto *status = w.findChild<QLabel *>(QStringLiteral("m_status"));
        r.check(tgt && tgt->toPlainText() == QLatin1String("ab\nB\ncd")
                    && status->text().contains(QStringLiteral("Done")),
                "window: source|translation streaming");
    }

    // 7. The clipboard of *this* platform. On Linux the row asks whether the
    // wl-clipboard reader is chosen only when the tools are there; on Windows
    // it asks whether the system clipboard round-trips text. What it may not do
    // is ask a Linux question on Windows and come back green: a check that
    // tests something the product does not do here is worse than no check,
    // because it teaches the reader that green means nothing.
    {
        const SelfTestRow row = probePlatformClipboard();
        if (!row.skipReason.isEmpty()) {
            r.skip(row.what, row.skipReason);
        } else {
            r.check(row.ok, qPrintable(row.what));
        }
    }

    // 7b. Unicode-escape safety net (BUG-1). The MIME half of this row is the
    // target negotiation both clipboard readers share, so it is a
    // platform-free check even though its two callers are per-platform.
    {
        const QString esc = QStringLiteral(
            "\u0414\u043b\u044f \u043d\u0430\u0447\u0430\u043b\u0430");
        r.check(decodeIfEscaped(esc).startsWith(QStringLiteral("Для начала"))
                    && !looksEscaped(QStringLiteral("plain text with one \u0410 code ref"))
                    && TextTargets::pickBestMime(
                           {QStringLiteral("text/html"),
                            QStringLiteral("text/plain;charset=utf-8")})
                    == QLatin1String("text/plain;charset=utf-8"),
                "unicode: escaped-clipboard safety net + MIME negotiation");
    }

    // 8. Autostart, through the same IAutostart the tray menu drives - the one
    // this platform has, aimed at a scratch location of its own, and cleaned up
    // afterwards. `traduku --selftest` may not decide for the user whether
    // their app starts tomorrow morning.
    {
        const SelfTestRow row = probePlatformAutostart(
            QCoreApplication::applicationFilePath());
        if (!row.skipReason.isEmpty()) {
            r.skip(row.what, row.skipReason);
        } else {
            r.check(row.ok, qPrintable(row.what));
        }
    }

    // 9. Optional live end-to-end against the user's configured endpoint
    if (doLive) {
        // The same file the rest of the run uses: --config must not make the
        // live check silently test a different endpoint.
        Settings s(configPath.isEmpty() ? Settings::defaultFilePath() : configPath);
        TranslationEngine engine(&s);
        bool ok = false, done = false;
        QObject::connect(&engine, &TranslationEngine::finished,
                         [&ok, &done](const QString &) { ok = done = true; });
        QObject::connect(&engine, &TranslationEngine::failed,
                         [&done](const QString &e) {
                             done = true;
                             QTextStream(stderr) << "live: " << e << '\n';
                         });
        engine.translate(QStringLiteral("Hello, self-test."));
        while (!done) {
            QCoreApplication::processEvents();
        }
        r.check(ok, "live: translate via configured endpoint");
    }

    out << (r.failures == 0 ? "SELFTEST PASSED" : "SELFTEST FAILED") << '\n';
    return r.failures;
}

} // namespace lt
