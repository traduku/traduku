#include <QtTest>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>

#include "ui/TranslatorWindow.h"

using namespace lt;

class TestTranslatorWindow : public QObject
{
    Q_OBJECT
private slots:
    void beginShowsSourceAndClearsTarget()
    {
        TranslatorWindow w;
        w.beginTranslation(QStringLiteral("original text"),
                           QStringLiteral("Русский (Russian)"));
        auto *src = w.findChild<QTextEdit *>(QStringLiteral("m_source"));
        auto *tgt = w.findChild<QTextEdit *>(QStringLiteral("m_target"));
        auto *status = w.findChild<QLabel *>(QStringLiteral("m_status"));
        QVERIFY(src && tgt && status);
        QCOMPARE(src->toPlainText(), QString("original text"));
        QCOMPARE(tgt->toPlainText(), QString());
        QVERIFY(status->text().contains(QStringLiteral("Translating")));
        QVERIFY(status->text().contains(QStringLiteral("Russian")));
    }

    void streamingAppendsInOrder()
    {
        TranslatorWindow w;
        w.beginTranslation(QStringLiteral("src"), QStringLiteral("en"));
        w.appendChunk(QStringLiteral("Hello"));
        w.appendChunk(QStringLiteral(", world"));
        w.endTranslation();
        auto *tgt = w.findChild<QTextEdit *>(QStringLiteral("m_target"));
        auto *status = w.findChild<QLabel *>(QStringLiteral("m_status"));
        QCOMPARE(tgt->toPlainText(), QString("Hello, world"));
        QVERIFY(status->text().contains(QStringLiteral("Done")));
    }

    void failureShowsReason()
    {
        TranslatorWindow w;
        w.beginTranslation(QStringLiteral("src"), QStringLiteral("en"));
        w.translationFailed(QStringLiteral("boom 503"));
        auto *status = w.findChild<QLabel *>(QStringLiteral("m_status"));
        QVERIFY(status->text().contains(QStringLiteral("boom 503")));
        // partial output stays visible so the user sees where it stopped
        w.appendChunk(QStringLiteral("x"));   // late chunks after failure ignored
    }

    void lateChunksAfterEndIgnored()
    {
        TranslatorWindow w;
        w.beginTranslation(QStringLiteral("src"), QStringLiteral("en"));
        w.appendChunk(QStringLiteral("good"));
        w.endTranslation();
        w.appendChunk(QStringLiteral("late"));
        auto *tgt = w.findChild<QTextEdit *>(QStringLiteral("m_target"));
        QCOMPARE(tgt->toPlainText(), QString("good"));
    }

    void languageComboReflectsAndEmits()
    {
        TranslatorWindow w;
        auto *combo = w.findChild<QComboBox *>(QStringLiteral("m_language"));
        QVERIFY(combo);
        QVERIFY(combo->count() > 30);
        w.setTargetLanguage(QStringLiteral("fr"));
        QCOMPARE(combo->currentData().toString(), QString("fr"));
        QSignalSpy spy(&w, &TranslatorWindow::targetLanguagePicked);
        combo->setCurrentIndex(combo->findData(QStringLiteral("ja")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().last().toString(), QString("ja"));
    }

    void copyButtonEmitsTranslation()
    {
        TranslatorWindow w;
        w.beginTranslation(QStringLiteral("src"), QStringLiteral("en"));
        w.appendChunk(QStringLiteral("translated"));
        w.endTranslation();
        QSignalSpy spy(&w, &TranslatorWindow::copyRequested);
        auto *copy = w.findChild<QPushButton *>(QStringLiteral("m_copy"));
        QVERIFY(copy);
        QVERIFY(copy->isEnabled());
        QTest::mouseClick(copy, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().last().toString(), QString("translated"));
    }

    void copyDisabledWithoutTranslation()
    {
        TranslatorWindow w;
        auto *copy = w.findChild<QPushButton *>(QStringLiteral("m_copy"));
        QVERIFY(!copy->isEnabled());
        w.beginTranslation(QStringLiteral("src"), QStringLiteral("en"));
        w.translationFailed(QStringLiteral("nope"));
        QVERIFY(!copy->isEnabled());
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
};

QTEST_MAIN(TestTranslatorWindow)
#include "tst_translatorwindow.moc"
