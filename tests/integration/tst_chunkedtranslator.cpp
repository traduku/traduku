#include <QtTest>
#include <QSignalSpy>

#include "core/chunkedtranslator.h"
#include "core/settings.h"
#include "core/llmstreamclient.h"
#include "mocksseserver.h"

using namespace lt;
using lt::test::MockSseServer;

class TestChunkedTranslator : public QObject
{
    Q_OBJECT
    QTemporaryDir dir;

    Settings *makeSettings(const QString &url)
    {
        auto *s = new Settings(dir.filePath(QStringLiteral("cfg%1.ini")
                                                .arg(m_n++)));
        s->setApiUrl(url);
        s->setModel(QStringLiteral("mock"));
        s->setTargetLanguage(QStringLiteral("ru"));
        s->save();
        return s;
    }
    int m_n = 0;

private slots:
    void documentStreamsRowByRow()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        ChunkedTranslator t(s, this);

        QSignalSpy layout(&t, &ChunkedTranslator::rowLayoutChanged);
        QSignalSpy committed(&t, &ChunkedTranslator::rowCommitted);
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        t.setSource(QStringLiteral("hello\nworld"));

        QTRY_COMPARE_WITH_TIMEOUT(committed.count(), 2, 10000);
        QVERIFY(done.count() >= 1);
        const QStringList rows = {t.rowText(0), t.rowText(1)};
        const QStringList want = {QStringLiteral("HELLO"), QStringLiteral("WORLD")};
        QCOMPARE(rows, want);
        QVERIFY(layout.count() >= 1);
        QCOMPARE(t.targetText(), QString("HELLO\nWORLD"));
        delete s;
    }

    void editRequeuesOnlyTouchedRow()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        ChunkedTranslator t(s, this);
        t.setSource(QStringLiteral("hello\nworld"));
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() >= 1, 10000);
        const int reqsBefore = server.requestCount();

        QSignalSpy committed(&t, &ChunkedTranslator::rowCommitted);
        t.applyEdit(QStringLiteral("hello\nworld2"));
        QTRY_COMPARE_WITH_TIMEOUT(committed.count(), 1, 10000);
        QCOMPARE(committed.last().at(0).toInt(), 1);       // row 1 only
        QCOMPARE(committed.last().at(1).toString(), QString("WORLD2"));
        QCOMPARE(t.targetText(), QString("HELLO\nWORLD2"));
        QVERIFY(server.requestCount() > reqsBefore);
        QTRY_VERIFY_WITH_TIMEOUT(t.isComplete(), 5000);
        delete s;
    }

    void streamingPreviewVisibleMidDocument()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        ChunkedTranslator t(s, this);
        QSignalSpy streaming(&t, &ChunkedTranslator::rowStreaming);
        t.setSource(QStringLiteral("alpha\nbeta\ngamma"));
        QTRY_VERIFY_WITH_TIMEOUT(streaming.count() >= 3, 10000);
        // Neighbours keep their finished translations while a later row
        // streams: row 0's final text is already in the document.
        QCOMPARE(t.rowText(0), QString("ALPHA"));
        delete s;
    }

    void editCancelsInFlightAndRelayouts()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        ChunkedTranslator t(s, this);
        QSignalSpy committed(&t, &ChunkedTranslator::rowCommitted);
        t.setSource(QStringLiteral("one\ntwo\nthree\nfour\nfive\nsix"));
        // Edit immediately, while early rows are still in flight.
        t.applyEdit(QStringLiteral("ONE modified\ntwo\nthree\nfour\nfive\nsix"));
        QTRY_VERIFY_WITH_TIMEOUT(t.isComplete(), 15000);
        QCOMPARE(t.rowText(0), QString("ONE MODIFIED"));
        QCOMPARE(t.rowText(5), QString("SIX"));
        delete s;
    }

    void failurePropagates()
    {
        Settings *s = makeSettings(QStringLiteral("http://127.0.0.1:1/v1"));
        ChunkedTranslator t(s, this);
        QSignalSpy failed(&t, &ChunkedTranslator::failed);
        t.setSource(QStringLiteral("hello"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        delete s;
    }
};

QTEST_MAIN(TestChunkedTranslator)
#include "tst_chunkedtranslator.moc"
