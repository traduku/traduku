#include <QtTest>
#include "core/chunker.h"

using namespace lt;

class TestChunker : public QObject
{
    Q_OBJECT

    static QString joined(const QString &s, int max = 1200)
    {
        QString out;
        for (const auto &c : splitChunks(s, max)) {
            out += c.text + c.sep;
        }
        return out;
    }

private slots:
    void joinIsIdentity_data()
    {
        QTest::addColumn<QString>("s");
        QTest::newRow("empty") << QString();
        QTest::newRow("one word") << QStringLiteral("Hello");
        QTest::newRow("paragraphs")
            << QStringLiteral("First para.\n\nSecond para.\n\n");
        QTest::newRow("single newlines")
            << QStringLiteral("AAA\nBBB\nCCC");
        QTest::newRow("leading whitespace")
            << QStringLiteral("\n\n  indented\nnext");
        QTest::newRow("crlf") << QStringLiteral("A\r\nB\r\n");
        QTest::newRow("tabs") << QStringLiteral("a\tb\t\nc");
        QTest::newRow("long paragraph")
            << QStringLiteral("Sentence one here. Sentence two here! Sentence "
                              "three? ").repeated(60);
        QTest::newRow("one huge word")
            << QStringLiteral("x").repeated(5000);
    }
    void joinIsIdentity()
    {
        QFETCH(QString, s);
        QCOMPARE(joined(s), s);
        QCOMPARE(joined(s, 40), s);
    }

    void splitsByLines()
    {
        const auto chunks = splitChunks(QStringLiteral("AAA\nBBB\nCCC"), 1200);
        QCOMPARE(chunks.size(), 3);
        QCOMPARE(chunks.at(0).text, QString("AAA"));
        QCOMPARE(chunks.at(0).sep, QString("\n"));
        QCOMPARE(chunks.at(2).text, QString("CCC"));
        QVERIFY(chunks.at(2).sep.isEmpty());
    }

    void blankLineParagraphs()
    {
        const auto chunks = splitChunks(QStringLiteral("P one.\n\nP two.\n"), 1200);
        QCOMPARE(chunks.size(), 2);
        QCOMPARE(chunks.at(0).sep, QString("\n\n"));
    }

    void longParagraphSplitsOnSentences()
    {
        const QString text = QStringLiteral("Alpha beta gamma. Delta epsilon! "
                                            "Zeta eta? Theta iota.")
                                 .repeated(50);                       // ~1750 chars
        const auto chunks = splitChunks(text, 300);
        QVERIFY(chunks.size() >= 5);
        for (const auto &c : chunks) {
            QVERIFY(c.text.size() <= 300 + 40);   // respects the cap loosely
            QVERIFY(c.text.contains(QLatin1Char(' ')));   // sentence-aligned,
                                                          // not mid-word
        }
    }

    void hugeWordHardSplits()
    {
        const QString text = QStringLiteral("x").repeated(1000);
        const auto chunks = splitChunks(text, 400);
        QVERIFY(chunks.size() >= 3);
        for (const auto &c : chunks) {
            QVERIFY(c.text.size() <= 400);
        }
    }

    void whitespaceOnlyIsOnePassthroughChunk()
    {
        const auto chunks = splitChunks(QStringLiteral("\n\n  \n"), 1200);
        QCOMPARE(chunks.size(), 1);
        QVERIFY(chunks.at(0).text.isEmpty());
    }

    void shortTextIsOneChunk()
    {
        const auto chunks = splitChunks(QStringLiteral("Hello world"), 1200);
        QCOMPARE(chunks.size(), 1);
        QCOMPARE(chunks.at(0).text, QString("Hello world"));
    }
};

QTEST_GUILESS_MAIN(TestChunker)
#include "tst_chunker.moc"
