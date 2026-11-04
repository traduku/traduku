#include <QtTest>
#include <QLocale>
#include "core/settings.h"

using namespace lt;

class TestSettings : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString path() const { return m_dir.path() + QStringLiteral("/config.ini"); }

private slots:
    void configPathHonoursEnvironmentOverride()
    {
        // "Run this one instance against another endpoint" must not require
        // inventing an XDG_CONFIG_HOME; tests and scripts need it too.
        const QString custom = m_dir.path() + QStringLiteral("/custom.ini");
        qputenv("TRADUKU_CONFIG", QFile::encodeName(custom));
        QCOMPARE(Settings::defaultFilePath(), custom);

        Settings s;                       // no explicit path -> the env wins
        QCOMPARE(s.filePath(), custom);
        s.setApiUrl(QStringLiteral("http://example.test/v1"));
        s.save();
        QVERIFY(QFile::exists(custom));

        qunsetenv("TRADUKU_CONFIG");
        QVERIFY2(!Settings::defaultFilePath().endsWith(QStringLiteral("custom.ini")),
                 "the override leaked past its lifetime");
    }

    void reasoningOptionsDefaultAndRoundTrip()
    {
        // A reasoning model streams a private channel first and we discard
        // it, so the default must be "ask the server to skip it".
        Settings s(path());
        QCOMPARE(s.disableReasoning(), true);
        QVERIFY2(s.reasoningOffOptions().contains(QStringLiteral("enable_thinking")),
                 qPrintable(s.reasoningOffOptions()));
        QVERIFY(s.extraOptions().isEmpty());

        s.setDisableReasoning(false);
        s.setExtraOptions(QStringLiteral("{\"top_p\":0.9}"));
        s.setReasoningOffOptions(QStringLiteral("{\"reasoning_effort\":\"none\"}"));
        s.save();

        Settings r(path());
        QCOMPARE(r.disableReasoning(), false);
        QCOMPARE(r.extraOptions(), QStringLiteral("{\"top_p\":0.9}"));
        QCOMPARE(r.reasoningOffOptions(), QStringLiteral("{\"reasoning_effort\":\"none\"}"));
    }

    void documentModeSettings()
    {
        Settings s(path());
        QCOMPARE(s.maxWholeTextChars(), 6000);      // one-request threshold
        QCOMPARE(s.maxParallel(), 4);               // chunk-mode pool
        s.setMaxWholeTextChars(-5);                 // clamp: 0 = document mode off
        s.setMaxParallel(99);                       // clamp to 8
        s.save();
        Settings r(path());
        QCOMPARE(r.maxWholeTextChars(), 0);
        QCOMPARE(r.maxParallel(), 8);
    }

    void autoTranslateDefaults()
    {
        Settings s(path());
        QCOMPARE(s.autoTranslate(), true);
        QCOMPARE(s.autoTranslateDelay(), 0.6);
        s.setAutoTranslate(false);
        s.setAutoTranslateDelay(99.0);      // clamp [0.1, 5.0]
        s.save();
        Settings r(path());
        QCOMPARE(r.autoTranslate(), false);
        QCOMPARE(r.autoTranslateDelay(), 5.0);
    }

    void defaultTargetLanguageFollowsLocale()
    {
        QCOMPARE(Settings::defaultTargetLanguageForLocale(QStringLiteral("de_DE")),
                 QString("de"));
        QCOMPARE(Settings::defaultTargetLanguageForLocale(QStringLiteral("ru_RU")),
                 QString("ru"));
        QCOMPARE(Settings::defaultTargetLanguageForLocale(QStringLiteral("quz_PE")),
                 QString("en"));   // unsupported locale -> neutral English
    }

    void configFileExistsReflectsDisk()
    {
        const QString fresh = m_dir.path() + QStringLiteral("/fresh.ini");
        QFile::remove(fresh);
        Settings s(fresh);
        QCOMPARE(s.configFileExists(), false);
        s.save();
        QCOMPARE(s.configFileExists(), true);
    }

    void defaultsWhenNoFile()
    {
        Settings s(path());
        QCOMPARE(s.apiUrl(), QString("http://localhost:11434/v1"));
        QCOMPARE(s.model(), QString("llama3.1"));
        QCOMPARE(s.targetLanguage(),
                   Settings::defaultTargetLanguageForLocale(QLocale::system().name()));   // changed from prototype's 'de'
        QCOMPARE(s.pressInterval(), 0.5);
        QCOMPARE(s.copyResult(), true);
        QCOMPARE(s.soundsEnabled(), true);
        QCOMPARE(s.globalShortcut(), QString("Ctrl+Alt+C"));
        QCOMPARE(s.apiKey(), QString());
        QCOMPARE(s.firstRunDone(), false);
    }

    void roundTripPersistence()
    {
        {
            Settings s(path());
            s.setApiUrl(QStringLiteral("http://127.0.0.1:9999/v1"));
            s.setApiKey(QStringLiteral("secret"));
            s.setModel(QStringLiteral("qwen"));
            s.setTargetLanguage(QStringLiteral("de"));
            s.setPressInterval(1.2);
            s.setCopyResult(false);
            s.setSoundsEnabled(false);
            s.setGlobalShortcut(QStringLiteral("Ctrl+Alt+T"));
            s.setFirstRunDone(true);
            QVERIFY(s.save());
            QVERIFY(QFile::exists(path()));
        }
        Settings s2(path());
        QCOMPARE(s2.apiUrl(), QString("http://127.0.0.1:9999/v1"));
        QCOMPARE(s2.apiKey(), QString("secret"));
        QCOMPARE(s2.model(), QString("qwen"));
        QCOMPARE(s2.targetLanguage(), QString("de"));
        QCOMPARE(s2.pressInterval(), 1.2);
        QCOMPARE(s2.copyResult(), false);
        QCOMPARE(s2.soundsEnabled(), false);
        QCOMPARE(s2.globalShortcut(), QString("Ctrl+Alt+T"));
        QCOMPARE(s2.firstRunDone(), true);
    }

    void changedSignalFires()
    {
        Settings s(path());
        QSignalSpy spy(&s, &Settings::changed);
        s.setModel(QStringLiteral("m2"));
        QCOMPARE(spy.count(), 1);
        s.setModel(QStringLiteral("m2"));   // no-op: no signal
        QCOMPARE(spy.count(), 1);
    }

    void reloadDiscardsUnsaved()
    {
        Settings s(path());
        s.setTargetLanguage(QStringLiteral("fr"));   // setter stores + saves? no: setters stage, save() persists
        s.save();
        s.setTargetLanguage(QStringLiteral("es"));   // staged, not saved
        s.reload();
        QCOMPARE(s.targetLanguage(), QString("fr"));
    }

    void intervalClamped()
    {
        Settings s(path());
        s.setPressInterval(0.0);      // clamp to [0.1, 3.0]
        QCOMPARE(s.pressInterval(), 0.1);
        s.setPressInterval(99.0);
        QCOMPARE(s.pressInterval(), 3.0);
    }

    void storedAsPlainIni()
    {
        Settings s(path());
        s.setApiUrl(QStringLiteral("http://x/v1"));
        s.save();
        QFile f(path());
        QVERIFY(f.open(QIODevice::ReadOnly));
        // Any DE/tool can edit it: plain key=value INI, group [General].
        const QString data = QString::fromUtf8(f.readAll());
        QVERIFY(data.contains(QStringLiteral("[General]")));
        QVERIFY(data.contains(QStringLiteral("ApiUrl=http://x/v1")));
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
