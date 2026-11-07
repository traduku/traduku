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
    // HiDPI: 200% is the default on a HiDPI laptop, not an option someone
    // digs for. The window asked for 980x560 logical pixels unconditionally,
    // which at scale 2 is 1960x1120 device pixels - on the 1280x800 grid
    // display that is a window whose bottom half (and quit affordance) is
    // beyond the screen, and a Wayland client cannot move it back.
    void theContentCanShrinkToWhatAScaledScreenOffers()
    {
        // fitToScreen can only give the window a size the layout will accept:
        // a top-level grows to its layout's minimum no matter what was asked.
        // Measured on ubuntu-gnome at QT_SCALE_FACTOR=2, the requested
        // 588x336 came out 841x336 - two labels and the language combo were
        // holding the window wider than the scaled screen, so the buttons sat
        // past the panel edge. A 640x400 logical work area (1280x800 at 200%)
        // has to be hostable.
        TranslatorWindow w;
        const QSize min = w.minimumSizeHint();
        QVERIFY2(min.width() <= 640,
                 qPrintable(QStringLiteral("content cannot shrink below %1 px wide "
                                            "(a 1280x800 screen at 200%% gives 640)")
                            .arg(min.width())));
        QVERIFY2(min.height() <= 400,
                 qPrintable(QStringLiteral("content cannot shrink below %1 px tall")
                            .arg(min.height())));
    }

    void aWindowThatFitsTheScreenIsLeftAlone()
    {
        QCOMPARE(TranslatorWindow::fitToScreen(QSize(980, 560), QSize(1280, 800)),
                 QSize(980, 560));
        QCOMPARE(TranslatorWindow::fitToScreen(QSize(980, 560), QSize(1920, 1080)),
                 QSize(980, 560));
    }

    void aWindowBiggerThanTheScreenIsScaledDownNotClipped()
    {
        // 1280x800 at 200% gives 640x400 logical.
        const QSize fit = TranslatorWindow::fitToScreen(QSize(980, 560), QSize(640, 400));
        QVERIFY2(fit.width() <= 640 && fit.height() <= 400,
                 qPrintable(QStringLiteral("still overflows the screen: ")
                            + QStringLiteral("%1x%2").arg(fit.width()).arg(fit.height())));
        // ... as a whole window: cropping one axis turns the panes into a
        // horizontal strip and the layout can never recover.
        const double wantedRatio = 980.0 / 560.0;
        const double gotRatio = double(fit.width()) / double(fit.height());
        QVERIFY2(qAbs(gotRatio - wantedRatio) / wantedRatio < 0.05,
                 qPrintable(QStringLiteral("aspect ratio lost: %1x%2").arg(fit.width())
                            .arg(fit.height())));
    }

    void fittingLeavesRoomForWhatTheCompositorOwns()
    {
        // Title bar, the top bar and window shadows belong to the compositor;
        // a window exactly the size of the work area is a window whose edges
        // cannot be reached.
        const QSize fit = TranslatorWindow::fitToScreen(QSize(980, 560), QSize(640, 400));
        QVERIFY2(fit.width() < 640 && fit.height() < 400,
                 qPrintable(QStringLiteral("no margin left: %1x%2").arg(fit.width())
                            .arg(fit.height())));
        // And never scaled to nothing: a 300x200 work area is a phone or a
        // tiled sliver, where the user needs the panes legible more than
        // they need the whole window on screen.
        const QSize tiny = TranslatorWindow::fitToScreen(QSize(980, 560), QSize(300, 200));
        QVERIFY2(tiny.width() >= 320 && tiny.height() >= 200,
                 qPrintable(QStringLiteral("scaled below use: %1x%2").arg(tiny.width())
                            .arg(tiny.height())));
    }

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
