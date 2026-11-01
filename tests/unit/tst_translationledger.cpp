#include <QtTest>
#include "core/translationledger.h"

using namespace lt;

class TestTranslationLedger : public QObject
{
    Q_OBJECT
private slots:
    void setSourceMarksContentPending()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("AAA\nBBB\n\nCCC"));
        QCOMPARE(l.slotCount(), 3);
        QCOMPARE(l.state(0), TranslationLedger::Pending);
        QCOMPARE(l.source(1), QString("BBB"));
        QCOMPARE(l.slotCount(), 3);            // "\n\n" merges into chunk sep
        QVERIFY(!l.isComplete());
        QCOMPARE(l.nextPending(), 0);
    }

    void whitespaceOnlySourceIsPassthrough()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("\n\n"));
        QCOMPARE(l.slotCount(), 1);
        QCOMPARE(l.state(0), TranslationLedger::Passthrough);
        QVERIFY(l.isComplete());               // nothing to translate
        QCOMPARE(l.sourceText(), QString("\n\n"));
    }

    void translationsFlowIntoTargetText()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("AAA\nBBB\nCCC"));
        l.setTranslation(0, QStringLiteral("zzz"), QStringLiteral("\n"));
        l.setTranslation(2, QStringLiteral("yyy"), QStringLiteral("\n"));
        // Pending slot shows its source as the placeholder
        QCOMPARE(l.targetText(), QString("zzz\nBBB\nyyy"));
        l.setTranslation(1, QStringLiteral("xxx"), QStringLiteral("\n"));
        QCOMPARE(l.targetText(), QString("zzz\nxxx\nyyy"));
        QVERIFY(l.isComplete());
    }

    void applyEditKeepsUnchangedTranslations()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("AAA\nBBB\nCCC"));
        for (int i = 0; i < l.slotCount(); ++i) {
            l.setTranslation(i, QStringLiteral("<%1>").arg(l.source(i)),
                             QStringLiteral("\n"));
        }
        QVERIFY(l.isComplete());

        const QVector<int> dirty = l.applyEdit(QStringLiteral("AAA\nBBB2\nCCC"));
        // Only the changed line is queued; neighbours keep their results.
        QCOMPARE(dirty.size(), 1);
        QCOMPARE(l.slotCount(), 3);
        QCOMPARE(l.source(dirty.at(0)), QString("BBB2"));
        QCOMPARE(l.state(0), TranslationLedger::Translated);
        QCOMPARE(l.slotCount(), 3);
        QCOMPARE(l.state(2), TranslationLedger::Translated);
        QCOMPARE(l.targetText(), QString("<AAA>\nBBB2\n<CCC>"));
    }

    void applyEditInsertAndRemove()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("A\nB\nC"));
        l.setTranslation(0, QStringLiteral("a"), QStringLiteral("\n"));
        l.setTranslation(1, QStringLiteral("b"), QStringLiteral("\n"));
        l.setTranslation(2, QStringLiteral("c"), QStringLiteral("\n"));

        // Insert a new line at the top: existing translations keep position.
        QVector<int> dirty = l.applyEdit(QStringLiteral("NEW\nA\nB\nC"));
        QCOMPARE(dirty.size(), 1);
        QCOMPARE(l.slotCount(), 4);
        QCOMPARE(l.targetText(), QString("NEW\na\nb\nc"));

        // Remove the first (untranslated) line.
        dirty = l.applyEdit(QStringLiteral("A\nB\nC"));
        QCOMPARE(dirty.size(), 0);
        QCOMPARE(l.targetText(), QString("a\nb\nc"));

        // Remove a translated line: the others stay aligned.
        dirty = l.applyEdit(QStringLiteral("A\nC"));
        QCOMPARE(dirty.size(), 0);
        QCOMPARE(l.slotCount(), 2);
        QCOMPARE(l.targetText(), QString("a\nc"));
    }

    void duplicateLinesTranslateOnce()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("X\nX"));
        l.setTranslation(0, QStringLiteral("y"), QStringLiteral("\n"));
        // second identical source line stays pending: identical text does not
        // import a translation of a *pending* twin, and LCS pairs them 1:1
        QCOMPARE(l.state(1), TranslationLedger::Pending);
        l.setTranslation(1, QStringLiteral("y"), QString());
        QVERIFY(l.isComplete());
    }

    void partialsVisibleWhileTranslating()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("AAA\nBBB"));
        l.setState(0, TranslationLedger::Translating);
        l.setPartial(0, QStringLiteral("zz"));
        QCOMPARE(l.targetText(), QString("zz\nBBB"));
        l.setTranslation(0, QStringLiteral("zzz"), QStringLiteral("\n"));
        QCOMPARE(l.targetText(), QString("zzz\nBBB"));
    }

    void longParagraphEditOnlyMarksSentences()
    {
        const QString para = QStringLiteral("One two. Three four. Five six.");
        TranslationLedger l;
        l.setSource(para, 10);                    // forces sentence chunks
        QVERIFY(l.slotCount() >= 2);
        for (int i = 0; i < l.slotCount(); ++i) {
            l.setTranslation(i, l.source(i).toUpper(), QString());
        }
        QString edited = para;
        edited.replace(QStringLiteral("Three four"), QStringLiteral("Three FOUR"));
        const QVector<int> dirty = l.applyEdit(edited, 10);
        // only sentences containing the change are dirty
        QVERIFY(dirty.size() >= 1);
        for (int i : dirty) {
            QVERIFY(l.source(i).contains(QStringLiteral("FOUR")));
        }
        QVERIFY(l.isComplete() == false);
    }

    void editEqualsSourceNoop()
    {
        TranslationLedger l;
        l.setSource(QStringLiteral("AAA\nBBB"));
        l.setTranslation(0, QStringLiteral("a"), QStringLiteral("\n"));
        l.setTranslation(1, QStringLiteral("b"), QString());
        QVERIFY(l.applyEdit(QStringLiteral("AAA\nBBB")).isEmpty());
        QVERIFY(l.isComplete());
    }
};

QTEST_GUILESS_MAIN(TestTranslationLedger)
#include "tst_translationledger.moc"
