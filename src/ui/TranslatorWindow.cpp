#include "TranslatorWindow.h"

#include "core/languages.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>

namespace lt {

TranslatorWindow::TranslatorWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("LLM Translate"));
    resize(980, 560);

    auto *grid = new QGridLayout(this);
    grid->addWidget(new QLabel(QStringLiteral("Original")), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Translation")), 0, 1);

    m_source = new QTextEdit(this);
    m_source->setObjectName(QStringLiteral("m_source"));
    m_source->setReadOnly(true);
    m_source->setLineWrapMode(QTextEdit::WidgetWidth);
    grid->addWidget(m_source, 1, 0);

    m_target = new QTextEdit(this);
    m_target->setObjectName(QStringLiteral("m_target"));
    m_target->setReadOnly(true);
    m_target->setLineWrapMode(QTextEdit::WidgetWidth);
    grid->addWidget(m_target, 1, 1);

    auto *bar = new QHBoxLayout;
    m_language = new QComboBox(this);
    m_language->setObjectName(QStringLiteral("m_language"));
    for (const auto &lang : languages()) {
        m_language->addItem(lang.name, lang.code);
    }
    connect(m_language, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_programmatic) {
            return;
        }
        Q_EMIT targetLanguagePicked(m_language->currentData().toString());
    });
    bar->addWidget(m_language);

    auto *settingsBtn = new QPushButton(QStringLiteral("Settings…"), this);
    settingsBtn->setObjectName(QStringLiteral("m_settings"));
    connect(settingsBtn, &QPushButton::clicked,
            this, &TranslatorWindow::settingsRequested);
    bar->addWidget(settingsBtn);

    m_copy = new QPushButton(QStringLiteral("Copy translation"), this);
    m_copy->setObjectName(QStringLiteral("m_copy"));
    m_copy->setEnabled(false);
    connect(m_copy, &QPushButton::clicked, this, [this]() {
        Q_EMIT copyRequested(m_target->toPlainText());
    });
    bar->addWidget(m_copy);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("m_status"));
    m_status->setText(QStringLiteral("Copy text, press Ctrl+C twice."));
    bar->addWidget(m_status, /*stretch*/ 1);

    grid->addLayout(bar, 2, 0, 1, 2);
}

void TranslatorWindow::setTargetLanguage(const QString &code)
{
    const int idx = m_language->findData(code);
    if (idx >= 0 && idx != m_language->currentIndex()) {
        m_programmatic = true;
        m_language->setCurrentIndex(idx);
        m_programmatic = false;
    }
}

QString TranslatorWindow::targetLanguage() const
{
    return m_language->currentData().toString();
}

void TranslatorWindow::beginTranslation(const QString &sourceText,
                                        const QString &targetName)
{
    m_streaming = true;
    m_hasResult = false;
    m_copy->setEnabled(false);
    m_source->setPlainText(sourceText);
    m_target->clear();
    m_status->setText(QStringLiteral("Translating to %1 …").arg(targetName));
}

void TranslatorWindow::appendChunk(const QString &chunk)
{
    if (!m_streaming) {
        return;
    }
    m_target->moveCursor(QTextCursor::End);
    m_target->insertPlainText(chunk);
    m_target->ensureCursorVisible();
}

void TranslatorWindow::endTranslation()
{
    m_streaming = false;
    m_hasResult = !m_target->toPlainText().isEmpty();
    m_copy->setEnabled(m_hasResult);
    m_status->setText(QStringLiteral("Done (%1 characters).")
                          .arg(m_target->toPlainText().size()));
}

void TranslatorWindow::translationFailed(const QString &reason)
{
    m_streaming = false;
    m_hasResult = false;
    m_copy->setEnabled(false);
    m_status->setText(QStringLiteral("Failed: %1").arg(reason));
}

void TranslatorWindow::showForeground()
{
    showNormal();
    raise();
    activateWindow();
}

} // namespace lt
