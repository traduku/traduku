#include "SettingsDialog.h"

#include "core/languages.h"
#include "core/settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>

namespace lt {

SettingsDialog::SettingsDialog(Settings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("Translate Settings"));
    resize(560, 0);

    auto *form = new QFormLayout(this);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_apiUrl = new QLineEdit(this);
    m_apiUrl->setObjectName(QStringLiteral("m_apiUrl"));
    m_apiUrl->setPlaceholderText(QStringLiteral("http://localhost:8080/v1"));
    m_apiUrl->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("LLM API address (OpenAI-compatible):"), m_apiUrl);

    m_apiKey = new QLineEdit(this);
    m_apiKey->setObjectName(QStringLiteral("m_apiKey"));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("(optional)"));
    m_apiKey->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("API key:"), m_apiKey);

    m_model = new QLineEdit(this);
    m_model->setObjectName(QStringLiteral("m_model"));
    m_model->setPlaceholderText(QStringLiteral("gpt-4o-mini / llama3.1 / ..."));
    m_model->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("Model name:"), m_model);

    auto *combo = new QComboBox(this);
    combo->setObjectName(QStringLiteral("m_targetLanguage"));
    for (const auto &lang : languages()) {
        combo->addItem(lang.name, lang.code);
    }
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    form->addRow(QStringLiteral("Translate to (destination):"), combo);

    auto *spin = new QDoubleSpinBox(this);
    spin->setObjectName(QStringLiteral("m_pressInterval"));
    spin->setRange(0.1, 3.0);
    spin->setSingleStep(0.1);
    spin->setDecimals(1);
    spin->setSuffix(QStringLiteral(" s"));
    form->addRow(QStringLiteral("Double Ctrl+C window:"), spin);

    auto *copy = new QCheckBox(QStringLiteral("Replace clipboard with the translation"), this);
    copy->setObjectName(QStringLiteral("m_copyResult"));
    form->addRow(QString(), copy);

    auto *sounds = new QCheckBox(QStringLiteral("Play sounds when a translation starts / finishes"), this);
    sounds->setObjectName(QStringLiteral("m_soundsEnabled"));
    form->addRow(QString(), sounds);

    auto *autoTr = new QCheckBox(QStringLiteral("Auto-translate when editing the source text"), this);
    autoTr->setObjectName(QStringLiteral("m_autoTranslate"));
    form->addRow(QString(), autoTr);

    auto *delay = new QDoubleSpinBox(this);
    delay->setObjectName(QStringLiteral("m_autoTranslateDelay"));
    delay->setRange(0.1, 5.0);
    delay->setSingleStep(0.1);
    delay->setDecimals(1);
    delay->setSuffix(QStringLiteral(" s"));
    form->addRow(QStringLiteral("Idle delay before auto-translate:"), delay);

    auto *whole = new QSpinBox(this);
    whole->setObjectName(QStringLiteral("m_maxWholeText"));
    whole->setRange(0, 200000);
    whole->setSingleStep(1000);
    whole->setSpecialValueText(QStringLiteral("disabled (always chunked)"));
    form->addRow(QStringLiteral("Translate whole text at once up to:"), whole);

    auto *par = new QSpinBox(this);
    par->setObjectName(QStringLiteral("m_maxParallel"));
    par->setRange(1, 8);
    form->addRow(QStringLiteral("Parallel chunk requests:"), par);

    m_globalShortcut = new QLineEdit(this);
    m_globalShortcut->setObjectName(QStringLiteral("m_globalShortcut"));
    m_globalShortcut->setPlaceholderText(QStringLiteral("Ctrl+Alt+C"));
    form->addRow(QStringLiteral("Global shortcut (fallback, press twice):"),
                 m_globalShortcut);

    auto *test = new QPushButton(QStringLiteral("Test API"), this);
    test->setObjectName(QStringLiteral("m_testApi"));
    m_testResult = new QLabel(this);
    m_testResult->setObjectName(QStringLiteral("m_testResult"));
    m_testResult->setWordWrap(true);
    connect(test, &QPushButton::clicked, this, &SettingsDialog::testApi);
    form->addRow(test, m_testResult);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel
            | QDialogButtonBox::RestoreDefaults,
        this);
    buttons->setObjectName(QStringLiteral("m_buttons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);
    form->addRow(buttons);

    load();
}

void SettingsDialog::load()
{
    m_apiUrl->setText(m_settings->apiUrl());
    m_apiKey->setText(m_settings->apiKey());
    m_model->setText(m_settings->model());
    auto *combo = findChild<QComboBox *>(QStringLiteral("m_targetLanguage"));
    const int idx = combo->findData(m_settings->targetLanguage());
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    }
    findChild<QDoubleSpinBox *>(QStringLiteral("m_pressInterval"))
        ->setValue(m_settings->pressInterval());
    findChild<QCheckBox *>(QStringLiteral("m_copyResult"))
        ->setChecked(m_settings->copyResult());
    findChild<QCheckBox *>(QStringLiteral("m_soundsEnabled"))
        ->setChecked(m_settings->soundsEnabled());
    findChild<QCheckBox *>(QStringLiteral("m_autoTranslate"))
        ->setChecked(m_settings->autoTranslate());
    findChild<QDoubleSpinBox *>(QStringLiteral("m_autoTranslateDelay"))
        ->setValue(m_settings->autoTranslateDelay());
    findChild<QSpinBox *>(QStringLiteral("m_maxWholeText"))
        ->setValue(m_settings->maxWholeTextChars());
    findChild<QSpinBox *>(QStringLiteral("m_maxParallel"))
        ->setValue(m_settings->maxParallel());
    m_globalShortcut->setText(m_settings->globalShortcut());
}

void SettingsDialog::accept()
{
    m_settings->setApiUrl(QUrl::fromUserInput(m_apiUrl->text().trimmed()).toString());
    m_settings->setApiKey(m_apiKey->text());
    m_settings->setModel(m_model->text().trimmed());
    m_settings->setTargetLanguage(
        findChild<QComboBox *>(QStringLiteral("m_targetLanguage"))
            ->currentData().toString());
    m_settings->setPressInterval(
        findChild<QDoubleSpinBox *>(QStringLiteral("m_pressInterval"))->value());
    m_settings->setCopyResult(
        findChild<QCheckBox *>(QStringLiteral("m_copyResult"))->isChecked());
    m_settings->setSoundsEnabled(
        findChild<QCheckBox *>(QStringLiteral("m_soundsEnabled"))->isChecked());
    m_settings->setAutoTranslate(
        findChild<QCheckBox *>(QStringLiteral("m_autoTranslate"))->isChecked());
    m_settings->setAutoTranslateDelay(
        findChild<QDoubleSpinBox *>(QStringLiteral("m_autoTranslateDelay"))->value());
    m_settings->setMaxWholeTextChars(
        findChild<QSpinBox *>(QStringLiteral("m_maxWholeText"))->value());
    m_settings->setMaxParallel(
        findChild<QSpinBox *>(QStringLiteral("m_maxParallel"))->value());
    m_settings->setGlobalShortcut(m_globalShortcut->text().trimmed());
    m_settings->save();
    QDialog::accept();
}

void SettingsDialog::restoreDefaults()
{
    m_apiUrl->setText(QStringLiteral("http://localhost:11434/v1"));
    m_apiKey->clear();
    m_model->setText(QStringLiteral("llama3.1"));
    findChild<QComboBox *>(QStringLiteral("m_targetLanguage"))
        ->setCurrentIndex(
            findChild<QComboBox *>(QStringLiteral("m_targetLanguage"))
                ->findData(QStringLiteral("ru")));
    findChild<QDoubleSpinBox *>(QStringLiteral("m_pressInterval"))->setValue(0.5);
    findChild<QCheckBox *>(QStringLiteral("m_copyResult"))->setChecked(true);
    findChild<QCheckBox *>(QStringLiteral("m_soundsEnabled"))->setChecked(true);
    findChild<QCheckBox *>(QStringLiteral("m_autoTranslate"))->setChecked(true);
    findChild<QDoubleSpinBox *>(QStringLiteral("m_autoTranslateDelay"))->setValue(0.6);
    findChild<QSpinBox *>(QStringLiteral("m_maxWholeText"))->setValue(6000);
    findChild<QSpinBox *>(QStringLiteral("m_maxParallel"))->setValue(4);
    m_globalShortcut->setText(QStringLiteral("Ctrl+Alt+C"));
}

void SettingsDialog::testApi()
{
    const QString base = m_apiUrl->text().trimmed();
    m_testResult->setText(QStringLiteral("Testing %1 …").arg(base));
    QNetworkRequest req(QUrl(base + QStringLiteral("/models")));
    req.setTransferTimeout(10'000);
    if (!m_apiKey->text().isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + m_apiKey->text().toUtf8());
    }
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            m_testResult->setText(QStringLiteral("Endpoint reachable ✔"));
        } else {
            m_testResult->setText(QStringLiteral("Failed: %1").arg(reply->errorString()));
        }
    });
}

} // namespace lt
