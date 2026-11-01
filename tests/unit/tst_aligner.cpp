#include <QtTest>

#include "core/aligner.h"
#include "core/chunker.h"

using namespace lt;

class TestAligner : public QObject
{
    Q_OBJECT

    static QVector<Chunk> chunks(const QString &text)
    {
        return splitChunks(text, 1200);
    }

private slots:
    void sameCountPairsPositionally()
    {
        const auto s = chunks(QStringLiteral("Hello world.\nSecond line."));
        const auto t = chunks(QStringLiteral("Hallo Welt.\nZweite Zeile."));
        const QVector<QString> got = alignChunks(s, t);
        QCOMPARE(got.size(), s.size());
        QCOMPARE(got.at(0), QString("Hallo Welt."));
        QCOMPARE(got.at(1), QString("Zweite Zeile."));
    }

    void oneTargetLineServesSeveralSourceLines()
    {
        // Model merged two short sentences into one line. All source
        // chunks are 1 word, so the blank slot's exact position inside the
        // merged run is a genuine DP tie - assert the invariants instead:
        // first slot carries the merged pair, "Drei." present, exactly one
        // blank, order preserved.
        const auto s = chunks(QStringLiteral("One.\nTwo.\nThree."));
        const auto t = chunks(QStringLiteral("Eins. Zwei.\nDrei."));
        const QVector<QString> got = alignChunks(s, t);
        QCOMPARE(got.size(), 3);
        QCOMPARE(got.at(0), QString("Eins. Zwei."));
        int blanks = 0;
        int dreiAt = -1;
        for (int i = 0; i < 3; ++i) {
            if (got.at(i).isEmpty()) {
                ++blanks;
            }
            if (got.at(i) == QLatin1String("Drei.")) {
                dreiAt = i;
            }
        }
        QCOMPARE(blanks, 1);
        QVERIFY(dreiAt > 0);
    }

    void oneSourceLineSplitsIntoTwoTargetLines()
    {
        const auto s = chunks(QStringLiteral("Ein Satz mit Komma, der zweigt ab."));
        const auto t = chunks(QStringLiteral("Ein Satz mit Komma.\nDer zweigt ab."));
        const QVector<QString> got = alignChunks(s, t);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.at(0), QString("Ein Satz mit Komma.\nDer zweigt ab."));
    }

    void expansionIsTolerated()
    {
        // German expansions of 1.5-2x are normal, must not break pairing.
        QString src, tgt;
        for (int i = 0; i < 6; ++i) {
            src += QStringLiteral("Sentence number %1.\n").arg(i);
            tgt += QStringLiteral("Dies ist Satz Nummer %1 mit mehr Wortern.\n").arg(i);
        }
        const auto s = chunks(src), t = chunks(tgt);
        const QVector<QString> got = alignChunks(s, t);
        QCOMPARE(got.size(), s.size());
        QVERIFY(!got.at(3).isEmpty());
        QVERIFY(got.at(3).contains(QString::number(3)));
    }

    void garbageTargetFailsAlignment()
    {
        const auto s = chunks(QStringLiteral("Alpha bravo charlie.\nDelta echo.\nFoxtrot golf hotel india."));
        const auto t = chunks(QStringLiteral("Ja."));
        QCOMPARE(alignChunks(s, t).size(), 0);
    }

    void emptyTargetFailsAlignment()
    {
        const auto s = chunks(QStringLiteral("Some text here."));
        QCOMPARE(alignChunks(s, chunks(QString())).size(), 0);
    }

    void blankSourceSlotsIgnored()
    {
        const auto s = chunks(QStringLiteral("A one.\n\nB two."));
        const auto t = chunks(QStringLiteral("Eins A.\n\nZwei B."));
        const QVector<QString> got = alignChunks(s, t);
        QCOMPARE(got.size(), s.size());
        QCOMPARE(got.at(0), QString("Eins A."));
        QCOMPARE(got.at(s.size() - 1), QString("Zwei B."));
    }
};

QTEST_GUILESS_MAIN(TestAligner)
#include "tst_aligner.moc"
