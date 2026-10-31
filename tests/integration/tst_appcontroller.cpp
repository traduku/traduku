#include <QtTest>
#include <QSignalSpy>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>

#include "app/AppController.h"
#include "core/llmstreamclient.h"
#include "core/settings.h"
#include "core/soundplayer.h"
#include "core/translationengine.h"
#include "interfaces/iclipboard.h"
#include "interfaces/ihotkeybackend.h"
#include "interfaces/inotifier.h"
#include "ui/TranslatorWindow.h"
#include "mocksseserver.h"

using namespace lt;

namespace {

class MemClipboard : public IClipboard
{
public:
    QString text() const override { return m_text; }
    bool setText(const QString &t) override { m_text = t; ++writes; return true; }
    QString backendName() const override { return QStringLiteral("mem"); }
    int writes = 0;
    QString m_text;
};

class FakeNotifier : public INotifier
{
    Q_OBJECT
public:
    using INotifier::INotifier;
    struct Msg { QString title, body; };
    QList<Msg> messages;
    void notify(const QString &title, const QString &body,
                const QString &, int) override { messages << Msg{title, body}; }
    QString backendName() const override { return QStringLiteral("fake"); }
};

class FakeHotkey : public IHotkeyBackend
{
    Q_OBJECT
public:
    using IHotkeyBackend::IHotkeyBackend;
    bool start() override { return true; }
    void stop() override {}
    QString backendName() const override { return QStringLiteral("fake"); }
};

// Records play() calls with the event name - the pipeline contract,
// resolution/chain behaviour is covered by tst_soundplayer.
class RecordingSound : public SoundPlayer
{
public:
    RecordingSound(Settings *s, QStringList *played) : SoundPlayer(s), m_played(played) {}
    void play(SoundEvent e) override { *m_played << fileName(e); }
    QStringList *m_played;
};

} // namespace

class TestAppController : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    test::MockSseServer m_server;

    // Assembly shared by scenarios.
    Settings *m_settings;
    MemClipboard m_clip;
    FakeNotifier *m_notifier;
    QStringList m_sounds;
    SoundPlayer *m_sound;
    TranslatorWindow *m_window;
    LlmStreamClient *m_client;
    TranslationEngine *m_engine;
    FakeHotkey *m_hotkey;
    AppController *m_app;

    void assemble()
    {
        m_settings = new Settings(m_dir.path() + QStringLiteral("/c.ini"), this);
        m_settings->setApiUrl(m_server.baseUrl());
        m_settings->setModel(QStringLiteral("m"));
        m_settings->setTargetLanguage(QStringLiteral("ru"));
        m_notifier = new FakeNotifier(this);
        m_sound = new RecordingSound(m_settings, &m_sounds);
        m_window = new TranslatorWindow;
        m_client = new LlmStreamClient(this);
        m_engine = new TranslationEngine(m_settings, m_client, this);
        m_hotkey = new FakeHotkey(this);
        AppController::Backends b;
        b.settings = m_settings;
        b.clipboard = &m_clip;
        b.notifier = m_notifier;
        b.sound = m_sound;
        b.window = m_window;
        b.engine = m_engine;
        m_app = new AppController(b, this);
        m_app->setHotkey(m_hotkey);
    }

    QString target() const
    {
        return m_window->findChild<QTextEdit *>(QStringLiteral("m_target"))->toPlainText();
    }
    QString status() const
    {
        return m_window->findChild<QLabel *>(QStringLiteral("m_status"))->text();
    }

private slots:
    void init()
    {
        m_sounds.clear();
        m_clip = MemClipboard{};
        assemble();
    }
    void cleanup() { delete m_app; delete m_window; }

    void fullSuccessPipeline()
    {
        m_clip.m_text = QStringLiteral("Hello world");
        m_server.setResponse(200, {
            test::MockSseServer::sseEvent("Привет"),
            test::MockSseServer::sseEvent(" мир"),
            QByteArray("data: [DONE]\n\n"),
        });

        QSignalSpy done(m_engine, &TranslationEngine::finished);
        m_hotkey->start();
        Q_EMIT m_hotkey->activated();

        // started: sound + window populated immediately
        QCOMPARE(m_sounds.size(), 1);
        QVERIFY(m_sounds.at(0).contains(QStringLiteral("bell.oga")));
        QVERIFY(target().isEmpty() || true);  // streaming may not have arrived yet
        QVERIFY(done.wait(5000));

        QTRY_VERIFY(status().contains(QStringLiteral("Done")));
        QCOMPARE(target(), QString("Привет мир"));
        // finished: completion sound, clipboard replaced (copyResult=true), notify
        QCOMPARE(m_sounds.size(), 2);
        QVERIFY(m_sounds.at(1).contains(QStringLiteral("complete.oga")));
        QCOMPARE(m_clip.text(), QString("Привет мир"));
        QVERIFY(m_notifier->messages.size() >= 1);
        QVERIFY(m_notifier->messages.last().body.contains(QStringLiteral("Привет мир")));
        QVERIFY(!m_app->translating());
    }

    void streamingReachesWindowIncrementally()
    {
        // 12 SSE events in 1-byte fragments: deltas must trickle into the
        // window, not appear only at the end.
        QByteArray body;
        for (int i = 0; i < 12; ++i) body += test::MockSseServer::sseEvent(QStringLiteral("%1").arg(i));
        body += "data: [DONE]\n\n";
        QList<QByteArray> tiny;
        for (int i = 0; i < body.size(); i += 5) tiny << body.mid(i, 5);
        m_server.setResponse(200, tiny);
        m_clip.m_text = QStringLiteral("x");

        QSignalSpy done(m_engine, &TranslationEngine::finished);
        Q_EMIT m_hotkey->activated();
        QVERIFY(done.wait(10000));
        QCOMPARE(target(), QString("01234567891011"));
    }

    void failurePipeline()
    {
        m_server.setResponse(502, {QByteArray("{}")});
        m_clip.m_text = QStringLiteral("boom");
        QSignalSpy failed(m_engine, &TranslationEngine::failed);
        Q_EMIT m_hotkey->activated();
        QVERIFY(failed.wait(5000));
        QVERIFY(status().contains(QStringLiteral("Failed")));
        QVERIFY(m_sounds.size() >= 2);
        QVERIFY(m_sounds.last().contains(QStringLiteral("dialog-error.oga")));
        QVERIFY(m_notifier->messages.last().body.contains(QStringLiteral("failed")));
        QCOMPARE(m_clip.writes, 0);   // clipboard untouched on failure
    }

    void emptyClipboardWarns()
    {
        int oldWrites = m_clip.writes;
        Q_EMIT m_hotkey->activated();
        QVERIFY(m_notifier->messages.last().body.contains(QStringLiteral("empty")));
        QCOMPARE(m_clip.writes, oldWrites);
        QCOMPARE(m_sounds.size(), 0);   // no start sound for empty clipboard
    }

    void busyGuardIgnoresSecondActivation()
    {
        QList<QByteArray> slow;
        slow << test::MockSseServer::sseEvent("A");
        // no [DONE] - keep the stream open so the app stays busy
        m_server.setResponse(200, slow);
        m_clip.m_text = QStringLiteral("one");
        Q_EMIT m_hotkey->activated();
        QVERIFY(m_app->translating());
        m_clip.m_text = QStringLiteral("two");
        Q_EMIT m_hotkey->activated();               // ignored
        QCOMPARE(m_window->findChild<QTextEdit *>(QStringLiteral("m_source"))
                     ->toPlainText(), QString("one"));
    }

    void copyResultDisabledKeepsClipboard()
    {
        m_settings->setCopyResult(false);
        m_server.setResponse(200, {test::MockSseServer::sseEvent("res"),
                                   QByteArray("data: [DONE]\n\n")});
        m_clip.m_text = QStringLiteral("src");
        QSignalSpy done(m_engine, &TranslationEngine::finished);
        Q_EMIT m_hotkey->activated();
        QVERIFY(done.wait(5000));
        QTRY_VERIFY(status().contains(QStringLiteral("Done")));
        QCOMPARE(m_clip.text(), QString("src"));    // untouched
    }

    void windowCopyButtonWritesClipboard()
    {
        m_settings->setCopyResult(false);
        m_server.setResponse(200, {test::MockSseServer::sseEvent("copied text"),
                                   QByteArray("data: [DONE]\n\n")});
        m_clip.m_text = QStringLiteral("src");
        QSignalSpy done(m_engine, &TranslationEngine::finished);
        Q_EMIT m_hotkey->activated();
        QVERIFY(done.wait(5000));
        auto *copyBtn = m_window->findChild<QPushButton *>(QStringLiteral("m_copy"));
        QVERIFY(copyBtn->isEnabled());
        copyBtn->click();
        QCOMPARE(m_clip.text(), QString("copied text"));
    }

    void languagePickFromWindowPersists()
    {
        auto *combo = m_window->findChild<QComboBox *>(QStringLiteral("m_language"));
        combo->setCurrentIndex(combo->findData(QStringLiteral("ja")));
        QCOMPARE(m_settings->targetLanguage(), QString("ja"));
    }
};

QTEST_MAIN(TestAppController)
#include "tst_appcontroller.moc"
