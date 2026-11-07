#include <QtTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "core/settings.h"
#include "core/translationengine.h"
#include "core/llmstreamclient.h"
#include "mocksseserver.h"

using namespace lt;

class TestTranslationEngine : public QObject
{
    Q_OBJECT
    test::MockSseServer m_server;
    QTemporaryDir m_dir;

private slots:
    void promptCarriesAllConstraints()
    {
        const QString p = TranslationEngine::buildPrompt(
            QStringLiteral("Some text"), QStringLiteral("Русский (Russian)"));
        QVERIFY(p.contains(QStringLiteral("Русский (Russian)")));
        QVERIFY(p.contains(QStringLiteral("Some text")));
        QVERIFY(p.contains(QStringLiteral("ONLY the translated text")));
        QVERIFY(p.contains(QStringLiteral("auto")));  // source auto-detect
        QVERIFY(p.contains(QStringLiteral("<<<TEXT")));
        QVERIFY(p.contains(QStringLiteral("TEXT")));
        // marker-wrapped payload exactly:
        QVERIFY(p.contains(QStringLiteral("<<<TEXT\nSome text\nTEXT")));
    }

    void reasoningOptionsReachTheRequestBody()
    {
        // No QVERIFY inside: the macro's own `return;` breaks the deduced
        // return type.
        auto run = [this](bool disable, const QString &extras, bool *ok) {
            m_server.reset();
            m_server.setResponse(200, {test::MockSseServer::sseEvent("ok"),
                                      QByteArray("data: [DONE]\n\n")});
            Settings s(m_dir.path() + QStringLiteral("/r-%1-%2.ini")
                                     .arg(disable).arg(extras.size()));
            s.setApiUrl(m_server.baseUrl());
            s.setModel(QStringLiteral("test-model"));
            s.setDisableReasoning(disable);
            s.setExtraOptions(extras);
            s.save();

            TranslationEngine e(&s);
            QSignalSpy done(&e, &TranslationEngine::finished);
            e.translate(QStringLiteral("Some text"));
            *ok = done.wait(10000);
            return m_server.m_requests.last();
        };

        bool ok1 = false, ok2 = false;
        const QByteArray on = run(true, QStringLiteral("{\"top_p\":0.9}"), &ok1);
        QVERIFY(ok1);
        QVERIFY2(on.contains("chat_template_kwargs"), on);
        QVERIFY(on.contains("reasoning_effort"));
        QVERIFY2(on.contains("top_p"), on);

        const QByteArray off = run(false, QString(), &ok2);
        QVERIFY(ok2);
        QVERIFY2(!off.contains("reasoning_effort"),
                 "turning the switch off must not send the preset");
    }

    void pipelineStreamsDeltas()
    {
        m_server.setResponse(200, {
            test::MockSseServer::sseEvent("Pri"),
            test::MockSseServer::sseEvent("vet"),
            QByteArray("data: [DONE]\n\n"),
        });
        Settings settings(m_dir.path() + QStringLiteral("/config.ini"));
        settings.setApiUrl(m_server.baseUrl());
        settings.setModel(QStringLiteral("test-model"));
        settings.setTargetLanguage(QStringLiteral("ru"));

        LlmStreamClient client;
        TranslationEngine engine(&settings, &client);
        QSignalSpy started(&engine, &TranslationEngine::started);
        QSignalSpy deltas(&engine, &TranslationEngine::delta);
        QSignalSpy finished(&engine, &TranslationEngine::finished);
        QSignalSpy failed(&engine, &TranslationEngine::failed);

        engine.translate(QStringLiteral("Hello"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.last().at(0).toString(), QString("Hello"));
        QCOMPARE(started.last().at(1).toString(),
                 QString("Русский (Russian)"));   // language NAME, not code
        QVERIFY(engine.isBusy());
        QVERIFY(finished.wait(5000));
        QCOMPARE(deltas.count(), 2);
        QCOMPARE(finished.last().last().toString(), QString("Privet"));
        QCOMPARE(engine.translation(), QString("Privet"));
        QCOMPARE(failed.count(), 0);
        QVERIFY(!engine.isBusy());
    }

    void mockSeesWrappedSourceLanguage()
    {
        m_server.setResponse(200, {test::MockSseServer::sseEvent("ok"),
                                  QByteArray("data: [DONE]\n\n")});
        Settings settings(m_dir.path() + QStringLiteral("/c2.ini"));
        settings.setApiUrl(m_server.baseUrl());
        settings.setModel(QStringLiteral("m"));
        settings.setTargetLanguage(QStringLiteral("de"));
        settings.setApiKey(QStringLiteral("k"));
        LlmStreamClient client;
        TranslationEngine engine(&settings, &client);
        QSignalSpy finished(&engine, &TranslationEngine::finished);
        engine.translate(QStringLiteral("text to go"));
        QVERIFY(finished.wait(5000));
        const QByteArray raw = m_server.m_requests.last();
        QVERIFY(raw.contains("Deutsch (German)"));   // name resolved from code
        QVERIFY(raw.contains("<<<TEXT"));
        QVERIFY(raw.contains("text to go"));
    }

    void failurePropagatesAndUnlocks()
    {
        m_server.setResponse(500, {QByteArray("{}")});
        Settings settings(m_dir.path() + QStringLiteral("/c3.ini"));
        settings.setApiUrl(m_server.baseUrl());
        settings.setModel(QStringLiteral("m"));
        LlmStreamClient client;
        TranslationEngine engine(&settings, &client);
        QSignalSpy failed(&engine, &TranslationEngine::failed);
        engine.translate(QStringLiteral("x"));
        QVERIFY(engine.isBusy());
        QVERIFY(failed.wait(5000));
        QVERIFY(!engine.isBusy());
    }

    void busyGuardIgnoresSecondRequest()
    {
        m_server.setResponse(200, {test::MockSseServer::sseEvent("ok"),
                                  QByteArray("data: [DONE]\n\n")});
        Settings settings(m_dir.path() + QStringLiteral("/c4.ini"));
        settings.setApiUrl(m_server.baseUrl());
        settings.setModel(QStringLiteral("m"));
        LlmStreamClient client;
        TranslationEngine engine(&settings, &client);
        QSignalSpy started(&engine, &TranslationEngine::started);
        engine.translate(QStringLiteral("one"));
        engine.translate(QStringLiteral("two"));   // ignored: busy
        QCOMPARE(started.count(), 1);
        QCOMPARE(engine.sourceText(), QString("one"));
        QSignalSpy finished(&engine, &TranslationEngine::finished);
        QVERIFY(finished.wait(5000));
        m_server.setResponse(200, {test::MockSseServer::sseEvent("ok"),
                                  QByteArray("data: [DONE]\n\n")});
        engine.translate(QStringLiteral("three")); // unlocked: accepted
        QCOMPARE(started.count(), 2);
        QCOMPARE(engine.sourceText(), QString("three"));
    }

    void emptyTextRejected()
    {
        Settings settings(m_dir.path() + QStringLiteral("/c5.ini"));
        LlmStreamClient client;
        TranslationEngine engine(&settings, &client);
        QSignalSpy failed(&engine, &TranslationEngine::failed);
        QSignalSpy started(&engine, &TranslationEngine::started);
        engine.translate(QStringLiteral("   "));
        QCOMPARE(started.count(), 0);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.last().last().toString(), QStringLiteral("empty"));
    }
};

QTEST_GUILESS_MAIN(TestTranslationEngine)
#include "tst_translationengine.moc"
