#include "TranslatorWindow.h"

#include "core/languages.h"

#include <QDebug>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTextEdit>

namespace lt {

TranslatorWindow::TranslatorWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Traduku"));
    resize(980, 560);

    auto *grid = new QGridLayout(this);
    auto *srcLabel = new QLabel(QStringLiteral("Original (editable - paste, "
                                               "type; Ctrl+Enter translates)"));
    grid->addWidget(srcLabel, 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Translation")), 0, 1);

    m_source = new QTextEdit(this);
    m_source->setObjectName(QStringLiteral("m_source"));
    m_source->setReadOnly(false);
    m_source->setLineWrapMode(QTextEdit::WidgetWidth);
    m_source->setAcceptRichText(false);
    grid->addWidget(m_source, 1, 0);

    m_target = new QTextEdit(this);
    m_target->setObjectName(QStringLiteral("m_target"));
    m_target->setReadOnly(true);
    m_target->setLineWrapMode(QTextEdit::WidgetWidth);
    grid->addWidget(m_target, 1, 1);

    connect(m_source, &QTextEdit::textChanged, this, [this]() {
        if (!m_programmaticSource) {
            Q_EMIT sourceEdited();
        }
    });

    m_translateShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return),
                                        this);
    m_translateShortcut->setContext(Qt::WindowShortcut);
    connect(m_translateShortcut, &QShortcut::activated, this,
            &TranslatorWindow::translateRequested);

    auto *bar = new QHBoxLayout;
    m_language = new QComboBox(this);
    m_language->setObjectName(QStringLiteral("m_language"));
    for (const auto &lang : languages()) {
        m_language->addItem(lang.name, lang.code);
    }
    connect(m_language, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_programmaticLang) {
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
    m_status->setText(QStringLiteral("Copy text, press Ctrl+C twice - or "
                                     "type here and press Ctrl+Enter."));
    bar->addWidget(m_status, /*stretch*/ 1);

    grid->addLayout(bar, 2, 0, 1, 2);
}

void TranslatorWindow::setTargetLanguage(const QString &code)
{
    const int idx = m_language->findData(code);
    if (idx >= 0 && idx != m_language->currentIndex()) {
        m_programmaticLang = true;
        m_language->setCurrentIndex(idx);
        m_programmaticLang = false;
    }
}

QString TranslatorWindow::targetLanguage() const
{
    return m_language->currentData().toString();
}

QString TranslatorWindow::sourceText() const
{
    return m_source->toPlainText();
}

void TranslatorWindow::setSourceText(const QString &text)
{
    m_programmaticSource = true;
    m_source->setPlainText(text);
    m_programmaticSource = false;
}

void TranslatorWindow::showTargetRows(const QStringList &rows)
{
    m_rows = QVector<QString>(rows.cbegin(), rows.cend());
    m_committed = QVector<bool>(m_rows.size(), false);
    renderTarget();
    updateCopyEnabled();
}

void TranslatorWindow::streamIntoRow(int row, const QString &partial)
{
    if (row < 0 || row >= m_rows.size()) {
        return;   // stale stream after a re-layout
    }
    m_rows[row] = partial;
    renderTarget();
}

void TranslatorWindow::commitRow(int row, const QString &text)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    m_rows[row] = text;
    m_committed[row] = true;
    renderTarget();
    updateCopyEnabled();
}

void TranslatorWindow::renderTarget()
{
    // Re-rendering the whole document is O(n) per delta - fine for
    // document-sized texts, and immune to cursor-position drift while
    // streaming into the middle.
    m_target->setPlainText(m_rows.join(QLatin1Char('\n')));
}

void TranslatorWindow::updateCopyEnabled()
{
    // Enable once the document has any non-placeholder content: with the
    // ledger model, rows start as source placeholders, so "changed" is
    // the signal that real translation landed.
    m_copy->setEnabled(m_committed.contains(true));
}

void TranslatorWindow::statusTranslating(const QString &targetName)
{
    m_status->setText(QStringLiteral("Translating to %1 …").arg(targetName));
}

void TranslatorWindow::statusDone()
{
    m_status->setText(QStringLiteral("Done (%1 characters).")
                          .arg(m_target->toPlainText().size()));
}

void TranslatorWindow::statusFailed(const QString &reason)
{
    m_status->setText(QStringLiteral("Failed: %1").arg(reason));
}

void TranslatorWindow::showForeground()
{
    showNormal();
    raise();
    activateWindow();
    // The app's own witness that a window came up, with its title and the
    // size it mapped. On Plasma the grid could ask KWin's scripting API for
    // its window list; on GNOME there is no such API and a Wayland client may
    // not enumerate another client's windows at all, so this journal line
    // (the app runs as a user unit -> journalctl --user) plus a QMP
    // framebuffer diff is the DE-independent pair tests/e2e/test_06_window.py
    // asserts on. The title is part of the line for the same reason it is in
    // the KWin/xdotool answers: the witness must say *which* window it saw.
    qInfo("traduku: window shown \"%s\" %dx%d", qUtf8Printable(windowTitle()),
          width(), height());
}

} // namespace lt
