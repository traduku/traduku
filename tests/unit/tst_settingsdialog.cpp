#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>

#include "core/settings.h"
#include "ui/SettingsDialog.h"

using namespace lt;

class TestSettingsDialog : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString cfgPath() const { return m_dir.path() + QStringLiteral("/c.ini"); }

private slots:
    /// Every row starts from a config of its own. The suite used to share one
    /// file across all of them, which the row below documented in a comment
    /// ("autoTranslate stayed at the value loaded from disk ... set by
    /// loadsCurrentValues on this shared file") - a test that inherits state
    /// from its neighbour cannot tell you which of the two broke, and a row
    /// about defaults has no defaults left to read.
    void init() { QFile::remove(cfgPath()); }
    void loadsCurrentValues()
    {
        Settings s(cfgPath());
        s.setApiUrl(QStringLiteral("http://host:1/v1"));
        s.setModel(QStringLiteral("m1"));
        s.setTargetLanguage(QStringLiteral("pl"));
        s.setPressInterval(1.5);
        s.setCopyResult(false);
        s.setSoundsEnabled(false);
        s.save();

        SettingsDialog d(&s);
        QCOMPARE(d.findChild<QLineEdit *>(QStringLiteral("m_apiUrl"))->text(),
                 QString("http://host:1/v1"));
        QCOMPARE(d.findChild<QLineEdit *>(QStringLiteral("m_model"))->text(),
                 QString("m1"));
        QCOMPARE(d.findChild<QComboBox *>(QStringLiteral("m_targetLanguage"))
                     ->currentData().toString(), QString("pl"));
        QCOMPARE(d.findChild<QDoubleSpinBox *>(QStringLiteral("m_pressInterval"))
                     ->value(), 1.5);
        QCOMPARE(d.findChild<QCheckBox *>(QStringLiteral("m_copyResult"))
                     ->isChecked(), false);
        // The single "starts / finishes" box is gone (see
        // theSoundSectionIsTwoSwitchesNotOne below): one muted cue has to be
        // visible as one muted cue in the dialog. A muted app shows BOTH boxes
        // unchecked - the boxes show what the app will do, not what the keys
        // say under a master switch that overrides them.
        QVERIFY(!d.findChild<QCheckBox *>(QStringLiteral("m_soundsEnabled")));
        QCOMPARE(d.findChild<QCheckBox *>(QStringLiteral("m_soundsOnStart"))
                     ->isChecked(), false);
        QCOMPARE(d.findChild<QCheckBox *>(QStringLiteral("m_soundsOnFinish"))
                     ->isChecked(), false);
        s.setSoundsEnabled(true);
        s.setSoundOnStart(false);
        s.setSoundOnFinish(true);
        s.save();
        SettingsDialog dSound(&s);
        QCOMPARE(dSound.findChild<QCheckBox *>(QStringLiteral("m_soundsOnStart"))
                     ->isChecked(), false);
        QCOMPARE(dSound.findChild<QCheckBox *>(QStringLiteral("m_soundsOnFinish"))
                     ->isChecked(), true);
        s.setAutoTranslate(false);
        s.setAutoTranslateDelay(1.2);
        s.setMaxWholeTextChars(9000);
        s.setMaxParallel(6);
        s.save();
        SettingsDialog d2(&s);
        QCOMPARE(d2.findChild<QCheckBox *>(QStringLiteral("m_autoTranslate"))
                     ->isChecked(), false);
        QCOMPARE(d2.findChild<QDoubleSpinBox *>(QStringLiteral("m_autoTranslateDelay"))
                     ->value(), 1.2);
        QCOMPARE(d2.findChild<QSpinBox *>(QStringLiteral("m_maxWholeText"))
                     ->value(), 9000);
        QCOMPARE(d2.findChild<QSpinBox *>(QStringLiteral("m_maxParallel"))
                     ->value(), 6);
    }

    void acceptPersistsEdits()
    {
        Settings s(cfgPath());
        {
            SettingsDialog d(&s);
            d.findChild<QLineEdit *>(QStringLiteral("m_apiUrl"))
                ->setText(QStringLiteral("http://new:2/v1"));
            d.findChild<QLineEdit *>(QStringLiteral("m_apiKey"))
                ->setText(QStringLiteral("key"));
            d.findChild<QComboBox *>(QStringLiteral("m_targetLanguage"))
                ->setCurrentIndex(
                    d.findChild<QComboBox *>(QStringLiteral("m_targetLanguage"))
                        ->findData(QStringLiteral("zh-CN")));
            d.findChild<QLineEdit *>(QStringLiteral("m_globalShortcut"))
                ->setText(QStringLiteral("Ctrl+Alt+T"));
            d.findChild<QDoubleSpinBox *>(QStringLiteral("m_autoTranslateDelay"))
                ->setValue(2.5);
            d.findChild<QSpinBox *>(QStringLiteral("m_maxWholeText"))
                ->setValue(12000);
            d.findChild<QSpinBox *>(QStringLiteral("m_maxParallel"))
                ->setValue(3);
            d.findChild<QDialogButtonBox *>(QStringLiteral("m_buttons"))
                ->button(QDialogButtonBox::Ok)
                ->click();
        }
        Settings reread(cfgPath());
        QCOMPARE(reread.apiUrl(), QString("http://new:2/v1"));
        QCOMPARE(reread.apiKey(), QString("key"));
        QCOMPARE(reread.targetLanguage(), QString("zh-CN"));
        QCOMPARE(reread.globalShortcut(), QString("Ctrl+Alt+T"));
        QCOMPARE(reread.autoTranslateDelay(), 2.5);
        // With a config of its own, the dialog writes what its boxes showed:
        // auto-translate is on by default, and nothing in this row touched it.
        // (This row used to assert `false` and explain that the value came from
        // the previous row's file - that is the coupling init() removes.)
        QCOMPARE(reread.autoTranslate(), true);
        QCOMPARE(reread.maxWholeTextChars(), 12000);
        QCOMPARE(reread.maxParallel(), 3);
    }

    // ---- the sound section: two switches, each one a cue ----------------
    //
    // A checkbox whose label promises two things and whose state is one bit is
    // a control that cannot express the setting. ROADMAP.md §7.8, hand walk of
    // 2026-11-09: on Windows the balloon has its own voice, so "hear the
    // answer, not the keypress" is the common request, and the old box could
    // not say it.
    void theSoundSectionIsTwoSwitchesNotOne()
    {
        Settings s(cfgPath());
        s.save();
        SettingsDialog d(&s);
        auto *start = d.findChild<QCheckBox *>(QStringLiteral("m_soundsOnStart"));
        auto *finish = d.findChild<QCheckBox *>(QStringLiteral("m_soundsOnFinish"));
        QVERIFY(start && finish);
        QVERIFY2(start != finish, "both labels wired to one widget");
        QVERIFY(start->isChecked());
        QVERIFY(finish->isChecked());
        // Each label names one moment. A box labelled "starts / finishes" that
        // only controls one of them is the decoration this split exists to end.
        QVERIFY2(!start->text().contains(QStringLiteral("finish"), Qt::CaseInsensitive),
                 qPrintable(start->text()));
        QVERIFY2(!finish->text().contains(QStringLiteral("start"), Qt::CaseInsensitive),
                 qPrintable(finish->text()));
    }

    void theSoundSwitchesAreSourcesOfSettingsNotDecorations()
    {
        Settings s(cfgPath());
        s.save();
        {
            SettingsDialog d(&s);
            d.findChild<QCheckBox *>(QStringLiteral("m_soundsOnStart"))->setChecked(false);
            d.findChild<QDialogButtonBox *>(QStringLiteral("m_buttons"))
                ->button(QDialogButtonBox::Ok)->click();
        }
        Settings reread(cfgPath());
        QCOMPARE(reread.soundOnStart(), false);
        QCOMPARE(reread.soundOnFinish(), true);
        QVERIFY2(reread.soundsEnabled(),
                 "one cue muted is not a muted app - the report has to say so");
    }

    void uncheckingBothSoundSwitchesMutesTheApp()
    {
        Settings s(cfgPath());
        s.save();
        {
            SettingsDialog d(&s);
            d.findChild<QCheckBox *>(QStringLiteral("m_soundsOnStart"))->setChecked(false);
            d.findChild<QCheckBox *>(QStringLiteral("m_soundsOnFinish"))->setChecked(false);
            d.findChild<QDialogButtonBox *>(QStringLiteral("m_buttons"))
                ->button(QDialogButtonBox::Ok)->click();
        }
        Settings reread(cfgPath());
        QVERIFY(!reread.soundOnStart());
        QVERIFY(!reread.soundOnFinish());
        QVERIFY2(!reread.soundsEnabled(),
                 "nothing is left to play: the master switch has to agree, or "
                 "--backend-info reports a live sound path on a muted app");
    }

    void cancelKeepsOldValues()
    {
        Settings s(cfgPath());
        s.setApiUrl(QStringLiteral("http://keep/v1"));
        s.save();
        {
            SettingsDialog d(&s);
            d.findChild<QLineEdit *>(QStringLiteral("m_apiUrl"))
                ->setText(QStringLiteral("http://discard/v1"));
            d.findChild<QDialogButtonBox *>(QStringLiteral("m_buttons"))
                ->button(QDialogButtonBox::Cancel)
                ->click();
        }
        Settings reread(cfgPath());
        QCOMPARE(reread.apiUrl(), QString("http://keep/v1"));
    }

    void restoreDefaultsResetsFields()
    {
        Settings s(cfgPath());
        s.setModel(QStringLiteral("weird"));
        s.save();
        SettingsDialog d(&s);
        d.findChild<QDialogButtonBox *>(QStringLiteral("m_buttons"))
            ->button(QDialogButtonBox::RestoreDefaults)
            ->click();
        QCOMPARE(d.findChild<QLineEdit *>(QStringLiteral("m_model"))->text(),
                 QString("llama3.1"));
        QCOMPARE(d.findChild<QLineEdit *>(QStringLiteral("m_apiUrl"))->text(),
                 QString("http://localhost:11434/v1"));
        QCOMPARE(d.findChild<QComboBox *>(QStringLiteral("m_targetLanguage"))
                     ->currentData().toString(), QString("ru"));
        QCOMPARE(d.findChild<QCheckBox *>(QStringLiteral("m_autoTranslate"))
                     ->isChecked(), true);
        QCOMPARE(d.findChild<QDoubleSpinBox *>(QStringLiteral("m_autoTranslateDelay"))
                     ->value(), 0.6);
        QCOMPARE(d.findChild<QSpinBox *>(QStringLiteral("m_maxWholeText"))
                     ->value(), 6000);
        QCOMPARE(d.findChild<QSpinBox *>(QStringLiteral("m_maxParallel"))
                     ->value(), 4);
    }

    void testApiButtonChecksEndpoint()
    {
        Settings s(cfgPath());
        s.setApiUrl(QStringLiteral("http://127.0.0.1:1/v1"));   // closed port
        s.save();
        SettingsDialog d(&s);
        auto *test = d.findChild<QPushButton *>(QStringLiteral("m_testApi"));
        auto *result = d.findChild<QLabel *>(QStringLiteral("m_testResult"));
        QVERIFY(test && result);
        test->click();
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains(QStringLiteral("Failed")),
                                 15000);
    }
};

QTEST_MAIN(TestSettingsDialog)
#include "tst_settingsdialog.moc"
