#include <QtTest>
#include <QComboBox>
#include <QShortcut>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>

#include "ui/TranslatorWindow.h"

using namespace lt;

class TestTranslatorWindow : public QObject
{
    Q_OBJECT
    QTextEdit *src(QWidget *w) const
    {
        return w->findChild<QTextEdit *>(QStringLiteral("m_source"));
    }
    QTextEdit *tgt(QWidget *w) const
    {
        return w->findChild<QTextEdit *>(QStringLiteral("m_target"));
    }
    QLabel *status(QWidget *w) const
    {
        return w->findChild<QLabel *>(QStringLiteral("m_status"));
    }

private slots:
    void sourceIsEditableByUser()
    {
        TranslatorWindow w;
        w.show();
        QVERIFY(!src(&w)->isReadOnly());
        QSignalSpy edited(&w, &TranslatorWindow::sourceEdited);
        src(&w)->setFocus();
        QTest::keyClicks(src(&w), QStringLiteral("hello"));
        QCOMPARE(src(&w)->toPlainText(), QString("hello"));
        QVERIFY(edited.count() >= 1);
    }

    void programmaticSetDoesNotEmitEdited()
    {
        TranslatorWindow w;
        QSignalSpy edited(&w, &TranslatorWindow::sourceEdited);
        w.setSourceText(QStringLiteral("from clipboard"));
        QCOMPARE(src(&w)->toPlainText(), QString("from clipboard"));
        QCOMPARE(w.sourceText(), QString("from clipboard"));
        QCOMPARE(edited.count(), 0);
    }

    void ctrlEnterRequestsTranslation()
    {
        TranslatorWindow w;
        w.show();
        w.activateWindow();
        src(&w)->setFocus();
        QCoreApplication::processEvents();
        QSignalSpy req(&w, &TranslatorWindow::translateRequested);
        QTest::keyClick(src(&w), Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(req.count(), 1, 2000);
    }

    void targetRowsRenderWithPlaceholders()
    {
        TranslatorWindow w;
        w.showTargetRows({QStringLiteral("A'"), QStringLiteral("BBB"),
                          QStringLiteral("C'")});
        QCOMPARE(tgt(&w)->toPlainText(), QString("A'\nBBB\nC'"));
        QVERIFY(!w.findChild<QPushButton *>(QStringLiteral("m_copy"))->isEnabled());
    }

    void streamingIntoMiddleRowKeepsNeighbours()
    {
        // The scenario the user asked to verify: neighbours already
        // translated, stream lands between them.
        TranslatorWindow w;
        w.showTargetRows({QStringLiteral("A'"), QStringLiteral("BBB"),
                          QStringLiteral("C'")});
        w.streamIntoRow(1, QStringLiteral("<B"));
        QCOMPARE(tgt(&w)->toPlainText(), QString("A'\n<B\nC'"));
        w.streamIntoRow(1, QStringLiteral("<BBB2>"));
        QCOMPARE(tgt(&w)->toPlainText(), QString("A'\n<BBB2>\nC'"));
        w.commitRow(1, QStringLiteral("<BBB2>"));
        QCOMPARE(tgt(&w)->toPlainText(), QString("A'\n<BBB2>\nC'"));
        QVERIFY(w.findChild<QPushButton *>(QStringLiteral("m_copy"))->isEnabled());
    }

    void commitStatusTransitions()
    {
        TranslatorWindow w;
        w.statusTranslating(QStringLiteral("Русский (Russian)"));
        QVERIFY(status(&w)->text().contains(QStringLiteral("Russian")));
        w.showTargetRows({QStringLiteral("x")});
        w.streamIntoRow(0, QStringLiteral("y"));
        w.commitRow(0, QStringLiteral("y"));
        w.statusDone();
        QVERIFY(status(&w)->text().contains(QStringLiteral("Done")));
        w.statusFailed(QStringLiteral("boom 503"));
        QVERIFY(status(&w)->text().contains(QStringLiteral("boom 503")));
    }

    void copyButtonEmitsRenderedTarget()
    {
        TranslatorWindow w;
        w.showTargetRows({QStringLiteral("A'"), QStringLiteral("B")});
        w.commitRow(0, QStringLiteral("A'"));
        QSignalSpy spy(&w, &TranslatorWindow::copyRequested);
        auto *copy = w.findChild<QPushButton *>(QStringLiteral("m_copy"));
        QVERIFY(copy->isEnabled());
        QTest::mouseClick(copy, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().last().toString(), QString("A'\nB"));
    }

    void languageComboReflectsAndEmits()
    {
        TranslatorWindow w;
        auto *combo = w.findChild<QComboBox *>(QStringLiteral("m_language"));
        QVERIFY(combo && combo->count() > 30);
        w.setTargetLanguage(QStringLiteral("fr"));
        QCOMPARE(combo->currentData().toString(), QString("fr"));
        QSignalSpy spy(&w, &TranslatorWindow::targetLanguagePicked);
        combo->setCurrentIndex(combo->findData(QStringLiteral("ja")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().last().toString(), QString("ja"));
    }

    void settingsButtonEmits()
    {
        TranslatorWindow w;
        QSignalSpy spy(&w, &TranslatorWindow::settingsRequested);
        auto *btn = w.findChild<QPushButton *>(QStringLiteral("m_settings"));
        QVERIFY(btn);
        QTest::mouseClick(btn, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
    }

    void lateStreamsIgnoredAfterNewLayout()
    {
        TranslatorWindow w;
        w.showTargetRows({QStringLiteral("one"), QStringLiteral("two")});
        w.showTargetRows({QStringLiteral("fresh")});   // re-edit resets rows
        w.streamIntoRow(5, QStringLiteral("stale"));   // out of range: ignored
        QCOMPARE(tgt(&w)->toPlainText(), QString("fresh"));
    }
};

QTEST_MAIN(TestTranslatorWindow)
#include "tst_translatorwindow.moc"
