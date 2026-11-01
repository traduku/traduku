#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace lt {

/// One chat-completions request against an OpenAI-compatible endpoint.
struct ChatRequest {
    QString baseUrl;        // e.g. http://localhost:11434/v1
    QString apiKey;         // Bearer token; empty = no Authorization header
    QString model;
    QString prompt;
    double temperature = 0.1;
    int transferTimeoutMs = 120'000;
};

/*!
 * Sends POST <baseUrl>/chat/completions with "stream": true and relays the
 * reply incrementally: delta() per content fragment as bytes arrive, then
 * exactly one of completed() / failed(). Uses SseParser, so partial chunks,
 * split UTF-8, [DONE] and server-side error payloads are handled.
 * One request at a time; send() while busy() is ignored (guarded by the
 * caller, TranslationEngine owns the busy-lock).
 */
class LlmStreamClient : public QObject
{
    Q_OBJECT
public:
    explicit LlmStreamClient(QObject *parent = nullptr);

    void send(const ChatRequest &request);
    void cancel();
    bool busy() const { return m_reply != nullptr; }

Q_SIGNALS:
    void delta(const QString &text);
    void completed();
    /// Emitted right before completed() when the stream ended with
    /// finish_reason == "length" (the model hit its output limit).
    void truncated();
    void failed(const QString &reason);

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    class SseParser *m_parser = nullptr;
    QString m_streamError;   // server-sent error seen mid-stream
};

} // namespace lt
