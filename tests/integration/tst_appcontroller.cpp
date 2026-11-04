#include <QtTest>
#include <functional>
#include <QSignalSpy>
#include <QDir>
#include <QLabel>
#include <QTextEdit>

#include "app/AppController.h"
#include "core/chunkedtranslator.h"
#include "core/settings.h"
#include "core/soundplayer.h"
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
    bool setText(const QString &t) override
    {
        m_text = t;
        ++writes;
        if (probe) {
            m_activeAtWrite = probe();
        }
        return true;
    }
    QString backendName() const override { return QStringLiteral("mem"); }
    int writes = 0;
    QString m_text;
    std::function<bool()> probe;   // e.g. "is the result window active?"
    bool m_activeAtWrite = false;
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

    Settings *m_settings;
    MemClipboard m_clip;
    FakeNotifier *m_notifier;
    QStringList m_sounds;
    TranslatorWindow *m_window;
    ChunkedTranslator *m_pipeline;
    FakeHotkey *m_hotkey;
    AppController *m_app;
    int m_n = 0;

    void assemble()
    {
        m_server.setEchoMode(true);
        m_settings = new Settings(m_dir.filePath(QStringLiteral("c%1.ini").arg(m_n++)), this);
        m_settings->setApiUrl(m_server.baseUrl());
        m_settings->setModel(QStringLiteral("m"));
        m_settings->setTargetLanguage(QStringLiteral("ru"));
        m_settings->setAutoTranslate(true);
        m_settings->setAutoTranslateDelay(0.1);
        m_notifier = new FakeNotifier(this);
        auto *sound = new RecordingSound(m_settings, &m_sounds);
        m_window = new TranslatorWindow;
        m_pipeline = new ChunkedTranslator(m_settings, this);
        m_hotkey = new FakeHotkey(this);
        AppController::Backends b;
        b.settings = m_settings;
        b.clipboard = &m_clip;
        b.notifier = m_notifier;
        b.sound = sound;
        b.window = m_window;
        b.pipeline = m_pipeline;
        m_app = new AppController(b, this);
        m_app->setHotkey(m_hotkey);
    }

    QString target() const
    {
        return m_window->findChild<QTextEdit *>(QStringLiteral("m_target"))->toPlainText();
    }
    QString source() const
    {
        return m_window->findChild<QTextEdit *>(QStringLiteral("m_source"))->toPlainText();
    }
    QString status() const
    {
        return m_window->findChild<QLabel *>(QStringLiteral("m_status"))->text();
    }
    QTextEdit *srcEdit() const
    {
        return m_window->findChild<QTextEdit *>(QStringLiteral("m_source"));
    }

private slots:
    void init()
    {
        m_sounds.clear();
        m_clip = MemClipboard{};
        assemble();
    }
    void cleanup() { delete m_app; delete m_window; }

    void gestureRunsDocument()
    {
        m_clip.m_text = QStringLiteral("hello world");
        Q_EMIT m_hotkey->activated();
        QTRY_COMPARE_WITH_TIMEOUT(target(), QString("HELLO WORLD"), 10000);
        QCOMPARE(source(), QString("hello world"));
        QVERIFY(m_sounds.contains("bell.oga"));
        QTRY_VERIFY(status().contains(QStringLiteral("Done")));
        QVERIFY(m_sounds.contains("complete.oga"));
        QCOMPARE(m_clip.text(), QString("HELLO WORLD"));   // copyResult
        QVERIFY(!m_app->translating());
    }

    void copyResultWritesFromActiveWindow()
    {
        // Wayland rejects clipboard writes (set_selection) from an app
        // without keyboard focus. The controller must activate the result
        // window before writing the translation, on every platform.
        m_clip.probe = [this] { return m_window->isActiveWindow(); };
        m_clip.m_text = QStringLiteral("activate me");
        Q_EMIT m_hotkey->activated();
        QTRY_COMPARE_WITH_TIMEOUT(m_clip.writes, 1, 10000);
        QCOMPARE(m_clip.text(), QString("ACTIVATE ME"));
        QVERIFY2(m_clip.m_activeAtWrite,
                 "clipboard write happened while the window was inactive: "
                 "Wayland compositors (kwin) drop set_selection silently");
    }

    void typingRequeuesOnlyEditedLine()
    {
        m_clip.m_text = QStringLiteral("hello\nworld");
        Q_EMIT m_hotkey->activated();
        QTRY_COMPARE_WITH_TIMEOUT(target(), QString("HELLO\nWORLD"), 10000);
        const int reqs = m_server.requestCount();

        // User edits the second line in the editable source pane.
        srcEdit()->setPlainText(QStringLiteral("hello\nWORLD2"));
        QTRY_VERIFY_WITH_TIMEOUT(m_server.requestCount() > reqs, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_app->translating(), 8000);
        QCOMPARE(target(), QString("HELLO\nWORLD2"));
        QVERIFY(m_server.requestCount() > reqs);          // one new request
        QVERIFY(m_server.requestCount() - reqs <= 2);     // not the document
    }

    void debounceCoalescesRapidTyping()
    {
        m_clip.m_text = QStringLiteral("a");
        Q_EMIT m_hotkey->activated();
        QTRY_COMPARE_WITH_TIMEOUT(target(), QString("A"), 10000);
        const int reqs = m_server.requestCount();

        srcEdit()->setPlainText(QStringLiteral("alpha"));
        QTest::qWait(30);   // within the 100ms debounce window
        QCOMPARE(m_server.requestCount(), reqs);   // nothing fired yet
        QTRY_VERIFY_WITH_TIMEOUT(target().contains(QStringLiteral("ALPHA")), 8000);
    }

    void autoTranslateOffWaitsForCtrlEnter()
    {
        m_settings->setAutoTranslate(false);
        m_clip.m_text = QStringLiteral("hello\nworld");
        Q_EMIT m_hotkey->activated();
        QTRY_COMPARE_WITH_TIMEOUT(target(), QString("HELLO\nWORLD"), 10000);
        const int reqs = m_server.requestCount();

        srcEdit()->setPlainText(QStringLiteral("hello\nworld3"));
        QTest::qWait(400);   // past the debounce window
        QCOMPARE(m_server.requestCount(), reqs);   // auto-translate off: idle

        // Ctrl+Enter forces translation of the touched line.
        m_window->show();
        m_window->activateWindow();
        srcEdit()->setFocus();
        QTest::keyClick(srcEdit(), Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(target().contains(QStringLiteral("WORLD3")), 10000);
    }

    void failureStatusSoundNotify()
    {
        m_settings->setApiUrl(QStringLiteral("http://127.0.0.1:1/v1"));
        m_clip.m_text = QStringLiteral("boom");
        Q_EMIT m_hotkey->activated();
        QTRY_VERIFY_WITH_TIMEOUT(status().contains(QStringLiteral("Failed")), 15000);
        QVERIFY(m_sounds.contains("dialog-error.oga"));
        QVERIFY(m_notifier->messages.last().body.contains(QStringLiteral("failed")));
        QCOMPARE(m_clip.writes, 0);
        QVERIFY(source().contains(QStringLiteral("boom")));   // text kept for retry
    }

    void copyButtonWritesClipboard()
    {
        m_clip.m_text = QStringLiteral("solo");
        Q_EMIT m_hotkey->activated();
        QTRY_COMPARE_WITH_TIMEOUT(target(), QString("SOLO"), 10000);
        int w = m_clip.writes;
        Q_EMIT m_window->copyRequested(QStringLiteral("SOLO"));
        QCOMPARE(m_clip.text(), QString("SOLO"));
        QVERIFY(m_clip.writes > w);
    }

    void paragraphsKeepBlankLinesInWindow()
    {
        m_clip.m_text = QStringLiteral("hello world\n\nsecond paragraph");
        Q_EMIT m_hotkey->activated();
        QTRY_VERIFY_WITH_TIMEOUT(status().contains(QStringLiteral("Done")), 10000);
        QCOMPARE(target(), QString("HELLO WORLD\n\nSECOND PARAGRAPH"));
    }

    void emptyClipboardWarns()
    {
        Q_EMIT m_hotkey->activated();
        QVERIFY(m_notifier->messages.last().body.contains(QStringLiteral("empty")));
        QCOMPARE(m_sounds.size(), 0);
    }
};

QTEST_MAIN(TestAppController)
#include "tst_appcontroller.moc"
