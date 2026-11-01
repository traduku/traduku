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
        s->setMaxWholeTextChars(0);   // force the chunk pipeline
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
        s->setMaxWholeTextChars(0);   // chunk pipeline under test
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
        s->setMaxWholeTextChars(0);   // chunk pipeline under test
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
        s->setMaxWholeTextChars(0);   // chunk pipeline under test
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

    void wholeTextSingleRequestMode()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        s->setMaxWholeTextChars(6000);
        ChunkedTranslator t(s, this);
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        t.setSource(QStringLiteral("hello\nworld"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QCOMPARE(server.requestCount(), 1);          // ONE request, full context
        QCOMPARE(t.rowText(0), QString("HELLO"));
        QCOMPARE(t.rowText(1), QString("WORLD"));
        QCOMPARE(t.targetText(), QString("HELLO\nWORLD"));
        delete s;
    }

    void documentModeOffUsesChunks()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        s->setMaxWholeTextChars(0);
        ChunkedTranslator t(s, this);
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        t.setSource(QStringLiteral("hello\nworld"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QVERIFY(server.requestCount() >= 2);
        delete s;
    }

    void truncatedDocumentFillsTailViaChunks()
    {
        MockSseServer server;
        server.setEchoMode(true);
        server.setTruncateFirstRequest(1);           // doc request -> "AA" only
        Settings *s = makeSettings(server.baseUrl());
        ChunkedTranslator t(s, this);
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        t.setSource(QStringLiteral("aa\nbb\ncc"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QCOMPARE(t.rowText(0), QString("AA"));
        QCOMPARE(t.rowText(1), QString("BB"));       // tail chunks translated
        QCOMPARE(t.rowText(2), QString("CC"));
        delete s;
    }

    void structureSquashFallsBackToChunks()
    {
        MockSseServer server;
        server.setEchoMode(true);
        server.setEchoSquash(true);                  // target: one flat line
        Settings *s = makeSettings(server.baseUrl());

        ChunkedTranslator t(s, this);
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        t.setSource(QStringLiteral("alpha here\nbeta there\ngamma too long line"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY(server.requestCount() >= 4);         // doc + 3 chunk retries
        QVERIFY(t.targetText().contains(QStringLiteral("ALPHA")));
        QVERIFY(t.targetText().contains(QStringLiteral("BETA")));
        delete s;
    }

    void previewStreamsDuringDocumentMode()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        ChunkedTranslator t(s, this);
        QSignalSpy streaming(&t, &ChunkedTranslator::rowStreaming);
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        t.setSource(QStringLiteral("alpha beta\ngamma delta"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QVERIFY(streaming.count() >= 1);             // live previews landed
        QCOMPARE(t.rowText(0), QString("ALPHA BETA"));
        QCOMPARE(t.rowText(1), QString("GAMMA DELTA"));
        delete s;
    }

    void paragraphBreaksRenderAsBlankRows()
    {
        MockSseServer server;
        server.setEchoMode(true);
        Settings *s = makeSettings(server.baseUrl());
        s->setMaxWholeTextChars(0);
        ChunkedTranslator t(s, this);
        QSignalSpy layout(&t, &ChunkedTranslator::rowLayoutChanged);
        QSignalSpy streaming(&t, &ChunkedTranslator::rowStreaming);
        QSignalSpy committed(&t, &ChunkedTranslator::rowCommitted);
        QSignalSpy done(&t, &ChunkedTranslator::allDone);
        t.setSource(QStringLiteral("alpha here\n\nbeta there"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        // Three display rows: paragraph gap = an empty row between them.
        const QStringList last = layout.last().at(0).toStringList();
        const QStringList want = {QStringLiteral("alpha here"), QString(),
                                  QStringLiteral("beta there")};
        QCOMPARE(last, want);
        QCOMPARE(committed.count(), 2);
        QCOMPARE(committed.at(0).at(0).toInt(), 0);
        QCOMPARE(committed.at(1).at(0).toInt(), 2);   // slot 1 -> display row 2
        QCOMPARE(t.targetText(), QString("ALPHA HERE\n\nBETA THERE"));
        // Streaming/commit row indices refer to DISPLAY rows (slot 1 -> row 2).
        bool sawRow2 = false;
        for (const auto &args : streaming) {
            if (args.at(0).toInt() == 2) {
                sawRow2 = true;
            }
        }
        QVERIFY(sawRow2);
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
