#include <QtTest>
#include <functional>
#include <QSignalSpy>
#include <QDir>
#include <QComboBox>
#include <QLabel>
#include <QRegularExpression>
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
    QString diagnostic() const override { return m_diagnostic; }
    int writes = 0;
    QString m_text;
    QString m_diagnostic;          // what the backend wants the user to know
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
    QString m_cfgPath;   // the file the assembled Settings writes, for "did it persist?"
    QtMessageHandler m_prevHandler = nullptr;

    // qInfo capture: the app's own journal lines are an observable product
    // surface (start_app runs the binary as a user unit, so they land in
    // `journalctl --user` and the e2e greps them there).
    static QStringList s_log;
    static void capture(QtMsgType, const QMessageLogContext &, const QString &m)
    {
        s_log.append(m);
    }

    QString logLine(const QString &needle) const
    {
        for (const QString &l : s_log) {
            if (l.contains(needle)) {
                return l;
            }
        }
        return QString();
    }

    void assemble()
    {
        m_server.setEchoMode(true);
        m_cfgPath = m_dir.filePath(QStringLiteral("c%1.ini").arg(m_n++));
        m_settings = new Settings(m_cfgPath, this);
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
        s_log.clear();
        // The server is one object for the whole class and it counts requests
        // for the rows below; reset() exists because of that (its own comment:
        // slots must not inherit each other's behaviour). Rows written as
        // deltas over a running baseline cannot tell "one request" from "the
        // eleven the previous rows left behind", so the baseline goes to zero
        // here and a count can be read as a count.
        m_server.reset();
        m_prevHandler = qInstallMessageHandler(&TestAppController::capture);
        m_clip = MemClipboard{};
        assemble();
    }
    void cleanup()
    {
        qInstallMessageHandler(m_prevHandler);
        delete m_app;
        delete m_window;
    }

    void gestureLogsTheWindowItMapped()
    {
        // The e2e (tests/e2e/test_06) asks "did the window actually come
        // up?". On Plasma it could ask KWin's scripting API; on GNOME there
        // is no such API and a Wayland client may not enumerate another
        // client's windows at all. So the app has to say it out loud, with
        // the size it mapped: pixels (QMP screendump) + this line are the
        // DE-independent pair the grid asserts on.
        m_clip.m_text = QStringLiteral("show yourself");
        Q_EMIT m_hotkey->activated();
        QTRY_VERIFY_WITH_TIMEOUT(logLine(QStringLiteral("window shown")).size() > 0,
                                 10000);
        const QString line = logLine(QStringLiteral("window shown"));
        const QRegularExpression re(
            QStringLiteral("window shown \"([^\"]*)\" (\\d+)x(\\d+)"));
        const QRegularExpressionMatch m = re.match(line);
        QVERIFY2(m.hasMatch(), qPrintable(QStringLiteral("no title/WxH in: ") + line));
        // The claim must be about the window the app actually has, not a
        // literal it prints regardless of what it shows.
        QCOMPARE(m.captured(1), m_window->windowTitle());
        QCOMPARE(m.captured(2).toInt(), m_window->width());
        QCOMPARE(m.captured(3).toInt(), m_window->height());
        QVERIFY(m_window->isVisible());
    }

    void aSecondLaunchSurfacesTheWindowTheMessagePromises()
    {
        // Both shells print "Traduku is already running (asked it to surface
        // its window)" when a second copy is launched - the message is the
        // interface for someone who clicked the icon and saw nothing. It was
        // a lie: SingleInstance::activateRequested (and KDBusService::Activate
        // on Plasma) had no connection anywhere, so the running instance did
        // nothing at all. The promise and the wiring are asserted together
        // here, because a message nobody can hear is how this stayed green.
        QVERIFY(!m_window->isVisible());
        m_app->surfaceWindow();
        QVERIFY2(m_window->isVisible(),
                 "the running instance was asked to surface its window and "
                 "left it hidden - the activation signal goes nowhere");
        QTRY_VERIFY_WITH_TIMEOUT(logLine(QStringLiteral("window shown")).size() > 0,
                                 10000);
    }

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

    // --- §7.8 item 4: the destination-language dropdown.
    //
    // Picking a language in the window's dropdown stored the code and waited
    // for the next gesture. A reader with a pane full of Russian who picks
    // German has asked a question about the text on screen, and "in the next
    // translation" is not an answer to it: the pick now translates the source
    // pane, the way the double-Ctrl+C gesture does.

    QComboBox *langCombo() const
    {
        return m_window->findChild<QComboBox *>(QStringLiteral("m_language"));
    }

    /// The tray's "Translate To" and the window's dropdown are the same
    /// function: QtTray fires the very same signal, ITrayBackend::
    /// targetLanguagePicked, and no shell had anything connected to it - a menu
    /// row that changes a setting behind the app's back, with no owner of the
    /// consequence. Picking a language in the tray now runs the same slot as
    /// picking it in the window.
    void trayPicksALanguageRunsTheSameSlotAsTheWindowDropdown()
    {
        m_window->setSourceText(QStringLiteral("hello world"));
        QCoreApplication::processEvents();
        QCOMPARE(m_server.requestCount(), 0);

        m_app->pickTargetLanguage(QStringLiteral("de"));   // what the tray emits into

        QCOMPARE(m_settings->targetLanguage(), QStringLiteral("de"));
        QCOMPARE(Settings(m_cfgPath).targetLanguage(), QStringLiteral("de"));  // persisted
        QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount(), 1, 10000);
        QVERIFY2(m_server.m_requests.at(0).contains("Deutsch"),
                 "the tray's language did not reach the request");
        QTRY_VERIFY_WITH_TIMEOUT(target().contains(QStringLiteral("HELLO WORLD")), 10000);
    }

    void pickingALanguageTranslatesTheSourcePane()
    {
        // setSourceText is the clipboard path: programmatic, no sourceEdited,
        // so the auto-translate timer is not part of what this row measures.
        m_window->setSourceText(QStringLiteral("hello world"));
        QCoreApplication::processEvents();
        QCOMPARE(m_server.requestCount(), 0);

        const int de = langCombo()->findData(QStringLiteral("de"));
        QVERIFY(de >= 0);
        langCombo()->setCurrentIndex(de);

        QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount(), 1, 10000);
        QVERIFY2(m_server.m_requests.at(0).contains("Deutsch"),
                 "the request was built for a language other than the one just picked");
        QTRY_VERIFY_WITH_TIMEOUT(target().contains(QStringLiteral("HELLO WORLD")), 10000);
        QCOMPARE(m_settings->targetLanguage(), QStringLiteral("de"));   // persisted, as before
    }

    /// applyEdit() keeps the translations of unchanged chunks - that is what
    /// makes editing one sentence cheap. Using it for a language change is a
    /// silent no-op, because no chunk changed, and the fix for "the dropdown
    /// does nothing" becomes a dropdown that does nothing. A re-translation has
    /// to be a new document.
    void aLanguageChangeAfterATranslationStartsANewOne()
    {
        m_window->setSourceText(QStringLiteral("hello world"));
        m_pipeline->setSource(QStringLiteral("hello world"));
        QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(target().contains(QStringLiteral("HELLO WORLD")), 10000);

        langCombo()->setCurrentIndex(langCombo()->findData(QStringLiteral("de")));
        QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount(), 2, 10000);
        QVERIFY2(m_server.m_requests.at(1).contains("Deutsch"),
                 "the second request did not ask for the newly picked language");
    }

    /// No text to translate: saving the code is the whole job. A request here
    /// would be an API call nobody asked for, and a notification would blame the
    /// user for an empty clipboard they never touched.
    void pickingALanguageOnAnEmptySourcePaneAsksNothing()
    {
        langCombo()->setCurrentIndex(langCombo()->findData(QStringLiteral("en")));
        QTest::qWait(400);
        QCOMPARE(m_server.requestCount(), 0);
        QCOMPARE(m_settings->targetLanguage(), QStringLiteral("en"));
        QVERIFY(m_notifier->messages.isEmpty());
    }

    /// The language the app sets from outside the dropdown - tray menu, a config
    /// file edited elsewhere, the watcher - is a choice about the next gesture,
    /// not a command to redo the last one. The window guards its programmatic
    /// writes for exactly this; the row keeps the guard load-bearing.
    void aLanguageSetFromOutsideTheDropdownTranslatesNothing()
    {
        m_window->setSourceText(QStringLiteral("hello world"));
        m_window->setTargetLanguage(QStringLiteral("de"));
        QTest::qWait(400);
        QCOMPARE(m_server.requestCount(), 0);
    }

    void emptyClipboardWarns()
    {
        Q_EMIT m_hotkey->activated();
        QVERIFY(m_notifier->messages.last().body.contains(QStringLiteral("empty")));
        QCOMPARE(m_sounds.size(), 0);
    }

    void emptyClipboardAdvisesTheMissingTool()
    {
        // Measured on GNOME 50 (ROADMAP.md §2): a background wayland
        // client receives no data offer, so "clipboard is empty" is a lie
        // when the real cause is a missing wl-clipboard. A backend that
        // knows it is blind carries the actionable hint, and the
        // notification must show THAT, not the misleading default.
        m_clip.m_text.clear();
        m_clip.m_diagnostic = QStringLiteral("install wl-clipboard");
        Q_EMIT m_hotkey->activated();
        QVERIFY2(m_notifier->messages.last().body.contains(QStringLiteral("wl-clipboard")),
                 qPrintable(m_notifier->messages.last().body));
        QCOMPARE(m_sounds.size(), 0);
    }
};

QStringList TestAppController::s_log;

QTEST_MAIN(TestAppController)
#include "tst_appcontroller.moc"
