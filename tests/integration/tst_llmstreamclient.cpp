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
        m_server.setResponse(200, {QByteArray("data: [DONE]\n\n")});
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
        m_server.setResponse(200, {QByteArray("data: [DONE]\n\n")});
        LlmStreamClient c;
        QSignalSpy done(&c, &LlmStreamClient::completed);
        c.send(request());
        QVERIFY(done.wait(5000));
        QVERIFY(!m_server.m_requests.last().toLower().contains("authorization"));
    }

    void trailingSlashInBaseUrl()
    {
        m_server.setResponse(200, {QByteArray("data: [DONE]\n\n")});
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
