#include "AppController.h"

#include "core/chunkedtranslator.h"
#include "core/languages.h"
#include "core/settings.h"
#include "core/soundplayer.h"
#include "core/unicodeescapes.h"
#include "interfaces/iclipboard.h"
#include "interfaces/ihotkeybackend.h"
#include "interfaces/inotifier.h"
#include "ui/TranslatorWindow.h"

#include <QDebug>
#include <QTimer>

namespace lt {

AppController::AppController(const Backends &b, QObject *parent)
    : QObject(parent)
    , m_b(b)
{
    connect(m_b.pipeline, &ChunkedTranslator::startedDocument, m_b.window,
            [this](const QString &source, const QString &targetName) {
                m_b.window->setSourceText(source);
                m_b.window->statusTranslating(targetName);
                m_b.window->showForeground();
                m_b.sound->play(SoundEvent::Started);
                qInfo("llm-translate: translating %d chars -> %s via %s (%s)",
                      int(source.size()), qPrintable(targetName),
                      qPrintable(m_b.settings->apiUrl()),
                      qPrintable(m_b.settings->model()));
            });
    connect(m_b.pipeline, &ChunkedTranslator::rowLayoutChanged, m_b.window,
            &TranslatorWindow::showTargetRows);
    connect(m_b.pipeline, &ChunkedTranslator::rowStreaming, m_b.window,
            &TranslatorWindow::streamIntoRow);
    connect(m_b.pipeline, &ChunkedTranslator::rowCommitted, m_b.window,
            &TranslatorWindow::commitRow);
    connect(m_b.pipeline, &ChunkedTranslator::allDone, m_b.window, [this]() {
        m_b.window->statusDone();
        m_b.sound->play(SoundEvent::Finished);
        const QString t = m_b.pipeline->targetText();
        QString clipboardNote;
        if (m_b.settings->copyResult()) {
            m_b.clipboard->setText(t);
            clipboardNote = QStringLiteral(" (clipboard updated)");
        }
        QString preview = t;
        if (preview.size() > 300) {
            preview = preview.left(300) + QStringLiteral(" …");
        }
        m_b.notifier->notify(
            QStringLiteral("LLM Translate"),
            QStringLiteral("Translated to %1%2:\n%3")
                .arg(languageNameOrCode(m_b.settings->targetLanguage()),
                     clipboardNote, preview));
    });
    connect(m_b.pipeline, &ChunkedTranslator::failed, m_b.window,
            [this](const QString &reason) {
                m_b.window->statusFailed(reason);
                m_b.sound->play(SoundEvent::Failed);
                m_b.notifier->notify(QStringLiteral("LLM Translate"),
                                     QStringLiteral("Translation failed: %1")
                                         .arg(reason));
                qWarning("llm-translate: %s", qUtf8Printable(reason));
            });

    // Interactive source pane: typing debounces into an incremental
    // applyEdit; Ctrl+Enter fires it immediately.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        if (m_b.settings->autoTranslate()) {
            m_b.pipeline->applyEdit(m_b.window->sourceText());
        }
    });
    connect(m_b.window, &TranslatorWindow::sourceEdited, this, [this]() {
        const int ms = qMax(50, int(m_b.settings->autoTranslateDelay() * 1000));
        m_debounce->start(ms);
    });
    connect(m_b.window, &TranslatorWindow::translateRequested, this, [this]() {
        m_debounce->stop();
        m_b.pipeline->applyEdit(m_b.window->sourceText());
    });

    connect(m_b.window, &TranslatorWindow::copyRequested, this,
            [this](const QString &text) {
                m_b.clipboard->setText(text);
                m_b.notifier->notify(QStringLiteral("LLM Translate"),
                                     QStringLiteral("Translation copied to "
                                                    "clipboard."));
            });
    connect(m_b.window, &TranslatorWindow::targetLanguagePicked, this,
            [this](const QString &code) {
                m_b.settings->setTargetLanguage(code);
                m_b.settings->save();
            });
    connect(m_b.window, &TranslatorWindow::settingsRequested,
            this, &AppController::settingsRequested);

    // Live-follow settings edited elsewhere (dialog, external edit):
    connect(m_b.settings, &Settings::changed, this, [this]() {
        m_b.window->setTargetLanguage(m_b.settings->targetLanguage());
    });
    m_b.window->setTargetLanguage(m_b.settings->targetLanguage());
}

void AppController::setHotkey(IHotkeyBackend *hotkey)
{
    connect(hotkey, &IHotkeyBackend::activated,
            this, &AppController::translateNow);
}

bool AppController::translating() const
{
    return !m_b.pipeline->isComplete();
}

void AppController::translateNow()
{
    QString text = decodeIfEscaped(m_b.clipboard->text().trimmed());
    if (text.isEmpty()) {
        m_b.notifier->notify(QStringLiteral("LLM Translate"),
                             QStringLiteral("Clipboard is empty."));
        return;
    }
    m_b.pipeline->setSource(text);
}

} // namespace lt
