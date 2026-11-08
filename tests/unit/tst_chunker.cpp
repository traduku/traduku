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
    /// A sentence boundary is `. ! ? …` - and not the digits that look like them.
    ///
    /// The class was written `[.!?\x2026]`, and PCRE reads that without braces as
    /// `\x20` (a space) followed by the literal characters `2` and `6`. So the
    /// boundary set was `. ! ? space 2 6`: a long block was cut after "Chapter 2 "
    /// or "1945 " as if a digit ended a sentence, and the ellipsis - the one
    /// character the escape was written for - was not a boundary at all.
    /// Measured, not inferred: both rows below fail on the old pattern.
    void digitsAreNotSentenceBoundaries()
    {
        // The false boundary has to sit *past* the cap, or nothing exercises it:
        // the sentence pass only flushes when a candidate is already over the
        // limit, and a candidate before it is merely remembered. Text with no
        // real sentence end until the full stop, and a " 2 " at index 55.
        const QString text = QStringLiteral(
            "Alpha bravo charlie delta echo foxtrot golf 2 hotel india india "
            "india bravo charlie.");
        const auto chunks = splitChunks(text, 40);
        QVERIFY(chunks.size() >= 2);
        for (const Chunk &c : chunks) {
            QVERIFY2(!c.text.trimmed().endsWith(QLatin1Char('2')),
                     qPrintable(QStringLiteral("a chunk was cut after a digit, "
                                               "because \"2 \" counted as a "
                                               "sentence end: [")
                                + c.text + QStringLiteral("]")));
        }
    }

    void anEllipsisIsASentenceBoundary()
    {
        // Same shape, past the cap: the ellipsis at index 33 is the only
        // boundary, so a correct splitter ends the first chunk there. The old
        // pattern did not know U+2026 (\x2026 without braces is a space plus
        // "26"), so it hard-split at the cap instead and the first chunk ended
        // mid-word.
        // The ellipsis is written \u2026, not \xE2\x80\xA6: QStringLiteral()
        // turns each byte of the source literal into its own QChar, so the
        // UTF-8 spelling would have tested a three-character mojibake - the very
        // bug the tray label had, caught here by the output not changing at all
        // while the code under test was demonstrably fixed.
        const QString text = QStringLiteral(
            "alpha beta gamma delta epsilon zeta\u2026 eta theta iota kappa.");
        const auto chunks = splitChunks(text, 20);
        QVERIFY2(chunks.size() >= 2,
                 qPrintable(QStringLiteral("the ellipsis did not end a chunk: ")
                            + QString::number(chunks.size()) + QStringLiteral(" chunk(s)")));
        QVERIFY2(chunks.first().text.trimmed().endsWith(QChar(0x2026)),
                 qPrintable(QStringLiteral("the first chunk did not end at the "
                                           "ellipsis: [")
                            + chunks.first().text + QStringLiteral("]")));
    }

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
