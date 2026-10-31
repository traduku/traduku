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
        QCOMPARE(d.findChild<QCheckBox *>(QStringLiteral("m_soundsEnabled"))
                     ->isChecked(), false);
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
            d.findChild<QDialogButtonBox *>(QStringLiteral("m_buttons"))
                ->button(QDialogButtonBox::Ok)
                ->click();
        }
        Settings reread(cfgPath());
        QCOMPARE(reread.apiUrl(), QString("http://new:2/v1"));
        QCOMPARE(reread.apiKey(), QString("key"));
        QCOMPARE(reread.targetLanguage(), QString("zh-CN"));
        QCOMPARE(reread.globalShortcut(), QString("Ctrl+Alt+T"));
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
