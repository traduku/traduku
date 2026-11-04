#include "llmstreamclient.h"

#include "sseparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace lt {

LlmStreamClient::LlmStreamClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void LlmStreamClient::send(const ChatRequest &req)
{
    if (m_reply) {
        return;
    }
    if (req.baseUrl.trimmed().isEmpty() || req.model.trimmed().isEmpty()
        || req.prompt.isEmpty()) {
        Q_EMIT failed(QStringLiteral(
            "API address and model name must be set, and the text non-empty."));
        return;
    }

    QString base = req.baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }

    const QJsonObject payload{
        {QStringLiteral("model"), req.model.trimmed()},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), req.prompt}}}},
        {QStringLiteral("temperature"), req.temperature},
        {QStringLiteral("stream"), true},
    };

    QNetworkRequest nr(QUrl(base + QStringLiteral("/chat/completions")));
    nr.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    nr.setTransferTimeout(req.transferTimeoutMs);
    if (!req.apiKey.isEmpty()) {
        nr.setRawHeader("Authorization",
                        "Bearer " + req.apiKey.trimmed().toUtf8());
    }

    delete m_parser;
    m_parser = new SseParser;
    m_reply = m_nam->post(nr, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &LlmStreamClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &LlmStreamClient::onFinished);
}

void LlmStreamClient::cancel()
{
    if (m_reply) {
        QPointer<LlmStreamClient> guard(this);
        m_reply->abort();          // finished() will fire and clean up
        Q_UNUSED(guard);
    }
}

void LlmStreamClient::onReadyRead()
{
    if (!m_reply || !m_parser) {
        return;
    }
    m_parser->append(m_reply->readAll());
    const QVector<QString> deltas = m_parser->takeDeltas();
    for (const QString &d : deltas) {
        Q_EMIT delta(d);
    }
    const QString serverError = m_parser->takeError();
    if (!serverError.isEmpty()) {
        // Mid-stream server error: surface it, drop the in-flight reply.
        m_streamError = serverError;
        m_reply->abort();
    }
}

void LlmStreamClient::onFinished()
{
    if (!m_reply) {
        return;
    }
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    if (!m_streamError.isEmpty()) {
        const QString e = m_streamError;
        m_streamError.clear();
        Q_EMIT failed(e);
        return;
    }

    // Drain any trailing buffered SSE line that arrived with the last bytes.
    if (m_parser) {
        m_parser->append(QByteArray("\n"));
        for (const QString &d : m_parser->takeDeltas()) {
            Q_EMIT delta(d);
        }
        const QString serverError = m_parser->takeError();
        if (!serverError.isEmpty()) {
            Q_EMIT failed(serverError);
            return;
        }
    }

    // Qt does not turn an HTTP 4xx/5xx into QNetworkReply::error(): a wrong
    // API key (401) or an nginx 502 would otherwise reach the caller as a
    // successful-but-empty translation. A JSON error body is still better
    // evidence, which is why this sits AFTER the parser-error branches.
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) {
        const QString phrase = reply->attribute(
            QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QString msg = QStringLiteral("HTTP %1").arg(status);
        if (!phrase.isEmpty()) {
            msg += QStringLiteral(" %1").arg(phrase);
        }
        Q_EMIT failed(msg + QStringLiteral(" from the API endpoint"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString reason = QStringLiteral("%1 (HTTP status %2)")
                             .arg(reply->errorString(),
                                  QString::number(reply->attribute(
                                      QNetworkRequest::HttpStatusCodeAttribute)
                                      .toInt()));
        Q_EMIT failed(reason);
        return;
    }
    if (m_parser->finishReason() == QLatin1String("length")) {
        Q_EMIT truncated();   // generation hit max_tokens: result is partial
    }
    Q_EMIT completed();
}

} // namespace lt
