#include "AppController.h"

#include "core/languages.h"
#include "core/unicodeescapes.h"
#include "core/settings.h"
#include "core/soundplayer.h"
#include "core/translationengine.h"
#include "interfaces/iclipboard.h"
#include "interfaces/ihotkeybackend.h"
#include "interfaces/inotifier.h"
#include "ui/TranslatorWindow.h"

#include <QDebug>

namespace lt {

AppController::AppController(const Backends &b, QObject *parent)
    : QObject(parent)
    , m_b(b)
{
    connect(m_b.engine, &TranslationEngine::started, m_b.window,
            [this](const QString &source, const QString &targetName) {
                m_b.window->beginTranslation(source, targetName);
                m_b.window->showForeground();
                m_b.sound->play(SoundEvent::Started);
                qInfo("llm-translate: translating %d chars -> %s via %s (%s)",
                      int(source.size()), qPrintable(targetName),
                      qPrintable(m_b.settings->apiUrl()),
                      qPrintable(m_b.settings->model()));
            });
    connect(m_b.engine, &TranslationEngine::delta,
            m_b.window, &TranslatorWindow::appendChunk);
    connect(m_b.engine, &TranslationEngine::finished, m_b.window, [this]() {
        // Safety net: some clipboard providers hand over text whose letters
        // arrived as literal \uXXXX JSON escapes (cp1251 pages, Klipper
        // history). Decode only demonstrably double-encoded strings.
        QString t = decodeIfEscaped(m_b.engine->translation());
        if (t != m_b.engine->translation()) {
            m_b.window->setTranslation(t);
        }
        m_b.window->endTranslation();
        m_b.sound->play(SoundEvent::Finished);
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
    connect(m_b.engine, &TranslationEngine::failed, m_b.window,
            [this](const QString &reason) {
                if (reason == QLatin1String("empty")) {
                    m_b.notifier->notify(QStringLiteral("LLM Translate"),
                                         QStringLiteral("The model returned an "
                                                        "empty translation."));
                    m_b.sound->play(SoundEvent::Failed);
                    return;
                }
                m_b.window->translationFailed(reason);
                m_b.sound->play(SoundEvent::Failed);
                m_b.notifier->notify(QStringLiteral("LLM Translate"),
                                     QStringLiteral("Translation failed: %1")
                                         .arg(reason));
                qWarning("llm-translate: %s", qUtf8Printable(reason));
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
    return m_b.engine->isBusy();
}

void AppController::translateNow()
{
    if (m_b.engine->isBusy()) {
        return;
    }
    QString text = decodeIfEscaped(m_b.clipboard->text().trimmed());
    if (text.isEmpty()) {
        m_b.notifier->notify(QStringLiteral("LLM Translate"),
                             QStringLiteral("Clipboard is empty."));
        return;
    }
    m_b.engine->translate(text);
}

} // namespace lt
