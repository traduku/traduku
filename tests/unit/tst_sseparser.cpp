#include <QtTest>
#include "core/sseparser.h"

using namespace lt;

class TestSseParser : public QObject
{
    Q_OBJECT
private slots:
    void singleChunk()
    {
        SseParser p;
        p.append("data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n");
        const auto d = p.takeDeltas();
        QCOMPARE(d.size(), 1);
        QCOMPARE(d.at(0), QString("Hello"));
        QCOMPARE(p.isDone(), false);
    }

    void multipleLinesInOneAppend()
    {
        SseParser p;
        p.append(QByteArray(
            "data: {\"choices\":[{\"delta\":{\"content\":\" a\"}}]}\n"
            "\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\" b\"}}]}\n"
            "\n"));
        const auto d = p.takeDeltas();
        QCOMPARE(d.join(QString()), QString(" a b"));
    }

    void splitAcrossAppends()
    {
        // bytes of one SSE line arriving in fragments, mid-line
        SseParser p;
        const QByteArray full =
            "data: {\"choices\":[{\"delta\":{\"content\":\"streamed\"}}]}\n\n";
        const QByteArray body = full.left(full.size() - 2);   // without \n\n
        for (int i = 0; i < body.size(); i += 3) {
            p.append(body.mid(i, 3));
            QCOMPARE(p.takeDeltas().size(), 0);   // nothing before the line ends
        }
        p.append("\n\n");
        const auto d = p.takeDeltas();
        QCOMPARE(d.size(), 1);
        QCOMPARE(d.at(0), QString("streamed"));
    }

    void utf8SplitMidCharacter()
    {
        // Multibyte UTF-8 (and JSON \u escapes incl. surrogate pairs) must
        // survive byte-fragmented transport.
        SseParser p;
        const QByteArray full =
            "data: {\"choices\":[{\"delta\":{\"content\":\"Привет \\uD83D\\uDE00\"}}]}\n\n";
        for (int i = 0; i < full.size(); i += 5) {
            p.append(full.mid(i, 5));
        }
        const auto d = p.takeDeltas();
        QCOMPARE(d.join(QString()), QString::fromUtf8("Привет \U0001F600"));
    }

    void doneMarker()
    {
        SseParser p;
        p.append("data: [DONE]\n\n");
        QCOMPARE(p.takeDeltas().size(), 0);
        QCOMPARE(p.isDone(), true);
    }

    void finishReasonIsNotContent()
    {
        SseParser p;
        p.append("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n");
        QCOMPARE(p.takeDeltas().size(), 0);   // empty delta carries no text
        QCOMPARE(p.isDone(), false);           // [DONE] may follow
    }

    void serverErrorEvent()
    {
        SseParser p;
        p.append("data: {\"error\":{\"message\":\"model overloaded\",\"type\":\"server_error\"}}\n\n");
        QCOMPARE(p.takeError(), QString("model overloaded"));
        QCOMPARE(p.isDone(), true);
    }

    void ignorableLines()
    {
        SseParser p;
        p.append(": comment line\nevent: message\nid: 42\n\n"
                 "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n");
        QCOMPARE(p.takeDeltas().join(QString()), QString("ok"));
    }

    void malformedJsonSkipped()
    {
        SseParser p;
        p.append("data: {not json}\n\n"
                 "data: {\"choices\":[{\"delta\":{\"content\":\"after\"}}]}\n\n");
        QCOMPARE(p.takeDeltas().join(QString()), QString("after"));
        QCOMPARE(p.takeErrorsSkipped(), 1);
    }

    void crlfLineEndings()
    {
        SseParser p;
        p.append("data: {\"choices\":[{\"delta\":{\"content\":\"crlf\"}}]}\r\n\r\n");
        QCOMPARE(p.takeDeltas().join(QString()), QString("crlf"));
    }

    void keepalivePing()
    {
        SseParser p;
        p.append("data: {}\n\n");
        QCOMPARE(p.takeDeltas().size(), 0);
        p.append("data: {\"choices\":[{\"delta\":{\"content\":\"x\"}}]}\n\n");
        QCOMPARE(p.takeDeltas().join(QString()), QString("x"));
    }
    void finishReasonExtracted()
    {
        SseParser p;
        p.append(QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"x\"},"
                                   "\"finish_reason\":null}]}\n\n"));
        p.append(QByteArrayLiteral("data: {\"choices\":[{\"delta\":{},"
                                   "\"finish_reason\":\"length\"}]}\n\n"));
        p.append(QByteArrayLiteral("data: [DONE]\n\n"));
        QVERIFY(p.isDone());
        QCOMPARE(p.finishReason(), QString("length"));
    }

    void finishReasonStop()
    {
        SseParser p;
        p.append(QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"y\"},"
                                   "\"finish_reason\":\"stop\"}]}\n\n"));
        QCOMPARE(p.finishReason(), QString("stop"));
    }

};

QTEST_GUILESS_MAIN(TestSseParser)
#include "tst_sseparser.moc"
