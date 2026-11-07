#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/llmstreamclient.h"
#include "mocksseserver.h"

using namespace lt;

class TestLlmStreamClient : public QObject
{
    Q_OBJECT
    test::MockSseServer m_server;

    static QByteArray sse(const QString &text)
    {
        return test::MockSseServer::sseEvent(text);
    }

    ChatRequest request() const
    {
        ChatRequest r;
        r.baseUrl = m_server.baseUrl();
        r.model = QStringLiteral("test-model");
        r.prompt = QStringLiteral("translate me");
        return r;
    }

private slots:
    void init()
    {
        // One server object is shared by all slots: a leftover "reject the
        // next N requests" from a previous slot would poison the next one.
        m_server.reset();
    }

    void streamsDeltasInOrder()
    {
        m_server.setResponse(200, {
            sse("Hello, "),
            sse("world"),
            QByteArray("data: [DONE]\n\n"),
        });
        LlmStreamClient c;
        QSignalSpy deltas(&c, &LlmStreamClient::delta);
        QSignalSpy done(&c, &LlmStreamClient::completed);
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        c.send(request());
        QVERIFY(done.wait(5000));
        QCOMPARE(fail.count(), 0);
        QStringList acc;
        for (const auto &a : deltas) acc << a.at(0).toString();
        QCOMPARE(acc.join(QString()), QString("Hello, world"));
    }

    void fragmentedStreamIsReassembled()
    {
        // Same stream, delivered in 7-byte fragments: the parser must stitch.
        QByteArray full = sse("Привет ми") + sse("р!") + QByteArray("data: [DONE]\n\n");
        QList<QByteArray> parts;
        for (int i = 0; i < full.size(); i += 7) parts << full.mid(i, 7);
        m_server.setResponse(200, parts);

        LlmStreamClient c;
        QSignalSpy deltas(&c, &LlmStreamClient::delta);
        QSignalSpy done(&c, &LlmStreamClient::completed);
        c.send(request());
        QVERIFY(done.wait(5000));
        QStringList acc;
        for (const auto &a : deltas) acc << a.at(0).toString();
        QCOMPARE(acc.join(QString()), QString("Привет мир!"));
    }

    void requestCarriesProtocolFields()
    {
        m_server.setResponse(200, {sse(QStringLiteral("ok")),
                                     QByteArray("data: [DONE]\n\n")});
        LlmStreamClient c;
        auto req = request();
        req.apiKey = QStringLiteral("sekrit");
        QSignalSpy done(&c, &LlmStreamClient::completed);
        c.send(req);
        QVERIFY(done.wait(5000));
        QVERIFY(!m_server.m_requests.isEmpty());
        const QByteArray &raw = m_server.m_requests.last();
        QVERIFY(raw.contains("POST /v1/chat/completions HTTP/1.1"));
        // HTTP field names are case-insensitive (RFC 7230); Qt 6.8 normalises
        // them to lowercase, Qt 6.12 keeps the submitted case — assert on the wire,
        // not on the case of the encoder.
        QVERIFY(raw.toLower().contains("authorization: bearer sekrit"));
        QVERIFY(raw.contains("\"stream\":true"));
        QVERIFY(raw.contains("\"model\":\"test-model\""));
        QVERIFY(raw.contains("translate me"));
    }

    void noApiKeyMeansNoAuthHeader()
    {
        m_server.setResponse(200, {sse(QStringLiteral("ok")),
                                     QByteArray("data: [DONE]\n\n")});
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        c.send(request());
        QVERIFY(done.wait(5000));
        QVERIFY(!m_server.m_requests.last().toLower().contains("authorization"));
    }

    void trailingSlashInBaseUrl()
    {
        m_server.setResponse(200, {sse(QStringLiteral("ok")),
                                     QByteArray("data: [DONE]\n\n")});
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        auto r = request();
        r.baseUrl += QStringLiteral("/");
        c.send(r);
        QVERIFY(done.wait(5000));
        QVERIFY(m_server.m_requests.last().contains("POST /v1/chat/completions"));
    }

    void httpErrorSurfaces()
    {
        m_server.setResponse(503, {QByteArray("{\"error\":{\"message\":\"no gpu\"}}")});
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        QSignalSpy done(&c, &LlmStreamClient::completed);
        c.send(request());
        QVERIFY(fail.wait(5000));
        QCOMPARE(done.count(), 0);
        QVERIFY(fail.last().last().toString().contains(QStringLiteral("503"))
                || fail.last().last().toString().contains(QStringLiteral("no gpu")));
    }

    // ---- server-specific request options (reasoning models) ------------

    void extraOptionsAreMergedIntoTheBody()
    {
        m_server.setResponse(200, {sse("ok")});
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        ChatRequest r = request();
        r.extraBodyJson = QStringLiteral("{\"seed\":7,\"user\":\"traduku\"}");
        c.send(r);
        QVERIFY(done.wait(5000));
        const QByteArray &body = m_server.m_requests.last();
        QVERIFY2(body.contains("\"seed\":7"), body);
        QVERIFY(body.contains("\"user\":\"traduku\""));
    }

    void reasoningOffInjectsThePresetByDefault()
    {
        // Measured on the live endpoint: this is what removes 37 "reasoning"
        // events and cuts 0.71 s to 0.21 s.
        m_server.setResponse(200, {sse("ok")});
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        ChatRequest r = request();
        r.disableReasoning = true;
        r.reasoningOffJson = QStringLiteral(
            "{\"chat_template_kwargs\":{\"enable_thinking\":false},\"reasoning_effort\":\"none\"}");
        c.send(r);
        QVERIFY(done.wait(5000));
        const QByteArray &body = m_server.m_requests.last();
        QVERIFY2(body.contains("chat_template_kwargs"), body);
        QVERIFY(body.contains("enable_thinking"));
        QVERIFY2(body.contains("reasoning_effort"), body);
    }

    void reasoningOffCanBeDisabled()
    {
        m_server.setResponse(200, {sse("ok")});
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        ChatRequest r = request();
        r.disableReasoning = false;
        r.reasoningOffJson = QStringLiteral("{\"reasoning_effort\":\"none\"}");
        c.send(r);
        QVERIFY(done.wait(5000));
        QVERIFY(!m_server.m_requests.last().contains("reasoning_effort"));
    }

    void userExtraOptionsWinOverThePreset()
    {
        m_server.setResponse(200, {sse("ok")});
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        ChatRequest r = request();
        r.disableReasoning = true;
        r.reasoningOffJson = QStringLiteral("{\"reasoning_effort\":\"none\"}");
        r.extraBodyJson = QStringLiteral("{\"reasoning_effort\":\"low\"}");
        c.send(r);
        QVERIFY(done.wait(5000));
        const QByteArray &body = m_server.m_requests.last();
        QVERIFY2(body.contains("\"reasoning_effort\":\"low\""), body);
        QVERIFY(!body.contains("\"reasoning_effort\":\"none\""));
    }

    void malformedOptionsFailWithAClearMessage()
    {
        // A typo in a settings field must not silently translate *with*
        // reasoning: the user has to see that the option was not applied.
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        ChatRequest r = request();
        r.extraBodyJson = QStringLiteral("{seed:7");
        c.send(r);
        QTRY_COMPARE_WITH_TIMEOUT(fail.count(), 1, 5000);
        const QString reason = fail.last().last().toString();
        QVERIFY2(reason.contains(QStringLiteral("Extra options")), qPrintable(reason));
        QVERIFY2(reason.contains(QStringLiteral("JSON")), qPrintable(reason));
        QCOMPARE(m_server.requestCount(), 0);      // nothing was sent
    }

    void strictServer400RetriesWithoutTheExtraOptions()
    {
        // Gateways that reject unknown fields must not lose the translation:
        // one retry with the optional extras dropped, then normal failure.
        m_server.setEchoMode(true);
        const int before = m_server.requestCount();
        m_server.setFailNextRequests(1, 400);
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        ChatRequest r = request();
        // Echo mode answers with what sits between the markers; without them
        // the reply is legitimately empty, which is now (correctly) a failure.
        r.prompt = QStringLiteral("<<<TEXT hello there TEXT");
        r.disableReasoning = true;
        r.reasoningOffJson = QStringLiteral("{\"reasoning_effort\":\"none\"}");
        c.send(r);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1 || fail.count() == 1, 12000);
        QCOMPARE(fail.count(), 0);
        QCOMPARE(m_server.requestCount(), before + 2);          // rejected + retried
        QVERIFY(m_server.m_requests.at(before).contains("reasoning_effort"));
        QVERIFY2(!m_server.m_requests.last().contains("reasoning_effort"),
                 "the retry must drop the options the server refused");
        QCOMPARE(c.extraOptionsRejectedCount(), 1);
    }

    void midStreamServerError()
    {
        m_server.setResponse(200, {
            sse("partial "),
            QByteArray("data: {\"error\":{\"message\":\"context overflow\"}}\n\n"),
        });
        LlmStreamClient c;
        QSignalSpy deltas(&c, &LlmStreamClient::delta);
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        c.send(request());
        QVERIFY(fail.wait(5000));
        QVERIFY(fail.last().last().toString().contains(QStringLiteral("context overflow")));
        QCOMPARE(deltas.count(), 1);   // what arrived before the error is kept
    }

    void connectionRefusedFails()
    {
        const quint16 port = m_server.serverPort();
        m_server.close();                       // nothing listens anymore
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        auto r = request();
        r.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
        c.send(r);
        QVERIFY(fail.wait(5000));
    }

    void invalidConfigFailsFast()
    {
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        auto r = request();
        r.baseUrl.clear();
        c.send(r);
        QCOMPARE(fail.count(), 1);              // synchronous, no request sent
        r = request();
        r.model.clear();
        c.send(r);
        QCOMPARE(fail.count(), 2);
        QVERIFY(c.busy() == false);
    }
    // ---- "the configuration is wrong", said clearly --------------------
    //
    // A user does not read "HTTP status 0". Each of these is what the operator
    // actually hits: a wrong key, a wrong URL, a server that is not up.

    static QUrl apiBase()
    {
        return QUrl(QStringLiteral("http://192.168.1.7:8000/v1"));
    }

    void wrongApiKeySaysCheckTheKey()
    {
        m_server.setResponse(401, {QByteArray("{\"error\":{\"message\":\"invalid api key\"}}")});
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        c.send(request());
        QVERIFY(fail.wait(5000));
        const QString msg = fail.last().last().toString();
        QVERIFY2(msg.contains(QStringLiteral("401")), qPrintable(msg));
        QVERIFY2(msg.contains(QStringLiteral("key"), Qt::CaseInsensitive),
                 qPrintable(msg));
    }

    void wrongUrlSaysWhichEndpointWasAsked()
    {
        // 404 is the usual result of a missing /v1 or a renamed model. The
        // message must contain the address, or the user guesses for an hour.
        m_server.setResponse(404, {QByteArray("{\"detail\":\"Not Found\"}")});
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        c.send(request());
        QVERIFY(fail.wait(5000));
        const QString msg = fail.last().last().toString();
        QVERIFY2(msg.contains(QStringLiteral("404")), qPrintable(msg));
        QVERIFY2(msg.contains(QStringLiteral("127.0.0.1")), qPrintable(msg));
    }

    void nothingListeningSaysSoAndNamesTheAddress()
    {
        const quint16 port = m_server.serverPort();
        m_server.close();
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        auto r = request();
        r.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
        c.send(r);
        QVERIFY(fail.wait(5000));
        const QString msg = fail.last().last().toString();
        QVERIFY2(msg.contains(QStringLiteral("127.0.0.1")), qPrintable(msg));
        // "HTTP status 0" alone told the user nothing (the old behaviour).
        QVERIFY2(!msg.trimmed().startsWith(QStringLiteral("HTTP status 0")),
                 qPrintable(msg));
    }

    void htmlWhereJsonWasExpectedIsNotAnEmptyTranslation()
    {
        // The failure mode a proxy login page produces: 200 OK, HTML, no
        // completion. Read as success it becomes "the model returned nothing",
        // which points at the model instead of at the URL.
        m_server.setResponse(200, {QByteArray(
            "<html><body>Please log in</body></html>\n")},
            QByteArray("text/html"));
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        QSignalSpy done(&c, &LlmStreamClient::completed);
        c.send(request());
        QVERIFY(fail.wait(5000));
        QCOMPARE(done.count(), 0);
        const QString msg = fail.last().last().toString();
        QVERIFY2(msg.contains(QStringLiteral("HTML"), Qt::CaseInsensitive)
                     || msg.contains(QStringLiteral("not"), Qt::CaseInsensitive),
                 qPrintable(msg));
    }

    void anEmptyCompletionBlamesTheAnswerNotTheClipboard()
    {
        m_server.setResponse(200, {test::MockSseServer::sseEvent(QString()),
                                   QByteArray("data: [DONE]\n\n")});
        LlmStreamClient c;
        QSignalSpy fail(&c, &LlmStreamClient::failed);
        c.send(request());
        QVERIFY(fail.wait(5000));
        const QString msg = fail.last().last().toString();
        QVERIFY2(msg.contains(QStringLiteral("no text"), Qt::CaseInsensitive)
                     || msg.contains(QStringLiteral("empty"), Qt::CaseInsensitive),
                 qPrintable(msg));
        QVERIFY2(msg.contains(QStringLiteral("model"), Qt::CaseInsensitive),
                 qPrintable(msg));
    }

    // The same sentences, from the pure formatters: no socket, no timing, and
    // the wording is what is actually under test here.
    void messageFormattersAreSpecific()
    {
        const QUrl url(QStringLiteral("http://aibox:8000/v1"));
        QVERIFY(LlmStreamClient::httpFailureMessage(403, QStringLiteral("Forbidden"), url)
                    .contains(QStringLiteral("key"), Qt::CaseInsensitive));
        QVERIFY(LlmStreamClient::httpFailureMessage(404, QStringLiteral("Not Found"), url)
                    .contains(QStringLiteral("model"), Qt::CaseInsensitive));
        QVERIFY(LlmStreamClient::httpFailureMessage(429, QStringLiteral("Too Many Requests"), url)
                    .contains(QStringLiteral("rate"), Qt::CaseInsensitive));
        QVERIFY(LlmStreamClient::httpFailureMessage(502, QStringLiteral("Bad Gateway"), url)
                    .contains(QStringLiteral("aibox")));
        QVERIFY(LlmStreamClient::httpFailureMessage(502, QString(), url)
                    .contains(QStringLiteral("502")));
        // Unknown status still names the endpoint: never an empty string.
        QVERIFY(!LlmStreamClient::httpFailureMessage(418, QString(), url).isEmpty());

        QVERIFY(LlmStreamClient::emptyCompletionMessage(url, QStringLiteral("text/html"))
                    .contains(QStringLiteral("HTML")));
        QVERIFY(LlmStreamClient::emptyCompletionMessage(url, QStringLiteral("text/event-stream"))
                    .contains(QStringLiteral("model"), Qt::CaseInsensitive));
    }

    void truncationSignalOnFinishReasonLength()
    {
        test::MockSseServer server;
        server.setResponse(200, {
            test::MockSseServer::sseEvent("half"),
            QByteArrayLiteral("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n\n"),
            QByteArrayLiteral("data: [DONE]\n\n"),
        });
        LlmStreamClient client;
        QSignalSpy truncated(&client, &LlmStreamClient::truncated);
        QSignalSpy completed(&client, &LlmStreamClient::completed);
        ChatRequest r = request();
        r.baseUrl = server.baseUrl();
        client.send(r);
        QVERIFY(completed.wait(5000));
        QCOMPARE(truncated.count(), 1);
    }

    void noTruncationOnStop()
    {
        test::MockSseServer server;
        server.setResponse(200, {
            QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"full\"},"
                              "\"finish_reason\":\"stop\"}]}\n\n"),
            QByteArrayLiteral("data: [DONE]\n\n"),
        });
        LlmStreamClient client;
        QSignalSpy truncated(&client, &LlmStreamClient::truncated);
        QSignalSpy completed(&client, &LlmStreamClient::completed);
        ChatRequest r = request();
        r.baseUrl = server.baseUrl();
        client.send(r);
        QVERIFY(completed.wait(5000));
        QCOMPARE(truncated.count(), 0);
    }

};

QTEST_GUILESS_MAIN(TestLlmStreamClient)
#include "tst_llmstreamclient.moc"
