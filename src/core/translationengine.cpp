#include "translationengine.h"

#include "languages.h"
#include "llmstreamclient.h"
#include "settings.h"

namespace lt {

TranslationEngine::TranslationEngine(Settings *settings, LlmStreamClient *client,
                                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (client) {
        m_client = client;
    } else {
        m_client = new LlmStreamClient(this);
        m_ownsClient = true;
    }
    connect(m_client, &LlmStreamClient::delta, this, [this](const QString &chunk) {
        m_result += chunk;
        Q_EMIT delta(chunk);
    });
    connect(m_client, &LlmStreamClient::completed, this, [this]() {
        m_busy = false;
        if (m_result.trimmed().isEmpty()) {
            Q_EMIT failed(QStringLiteral("empty"));
        } else {
            Q_EMIT finished(m_result);
        }
    });
    connect(m_client, &LlmStreamClient::truncated, this,
            &TranslationEngine::truncated);
    connect(m_client, &LlmStreamClient::failed, this, [this](const QString &reason) {
        m_busy = false;
        if (m_cancelled) {
            m_cancelled = false;   // cancel() is user-intent, not a failure
            return;
        }
        Q_EMIT failed(reason);
    });
}

QString TranslationEngine::buildPrompt(const QString &text,
                                       const QString &targetName)
{
    return QStringLiteral(
               "Translate the text between the markers into %1. Detect the "
               "source language automatically. Keep formatting, line breaks, "
               "code blocks and placeholders intact. Reply with ONLY the "
               "translated text - no explanations, no quotes, no preamble. "
               "If the text is already in %1, reply with it unchanged.\n\n"
               "<<<TEXT\n%2\nTEXT")
        .arg(targetName, text);
}

void TranslationEngine::cancel()
{
    if (m_busy) {
        m_cancelled = true;
        m_client->cancel();
    }
}

void TranslationEngine::translate(const QString &text)
{
    if (m_busy) {
        return;
    }
    if (text.trimmed().isEmpty()) {
        Q_EMIT failed(QStringLiteral("empty"));
        return;
    }
    m_source = text;
    m_result.clear();

    const QString targetName = languageNameOrCode(m_settings->targetLanguage());
    ChatRequest req;
    req.baseUrl = m_settings->apiUrl();
    req.apiKey = m_settings->apiKey();
    req.model = m_settings->model();
    req.prompt = buildPrompt(text, targetName);
    req.disableReasoning = m_settings->disableReasoning();
    req.reasoningOffJson = m_settings->reasoningOffOptions();
    req.extraBodyJson = m_settings->extraOptions();

    m_busy = true;
    Q_EMIT started(m_source, targetName);
    m_client->send(req);
}

} // namespace lt
