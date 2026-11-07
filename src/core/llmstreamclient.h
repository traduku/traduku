#pragma once

#include <QNetworkReply>   // the error enum appears in a public signature
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace lt {

/// One chat-completions request against an OpenAI-compatible endpoint.
struct ChatRequest {
    QString baseUrl;        // e.g. http://localhost:11434/v1
    QString apiKey;         // Bearer token; empty = no Authorization header
    QString model;
    QString prompt;
    double temperature = 0.1;
    int transferTimeoutMs = 120'000;

    /// Server-specific options merged into the request body (vLLM, llama.cpp
    /// and gateways each want their own). Invalid JSON fails the request
    /// loudly instead of translating without them.
    QString extraBodyJson;

    /// Many current chat models are reasoning models: they stream a private
    /// "reasoning" channel first, which we discard, so the user just waits.
    /// With this on, reasoningOffJson is merged in as well. Measured on
    /// tr-coder/vLLM: 37 reasoning events and 0.71 s drop to 0.21 s, and the
    /// answer no longer starts with the template's blank lines.
    bool disableReasoning = false;
    QString reasoningOffJson;   // e.g. {"chat_template_kwargs":{"enable_thinking":false}}
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

    /// How often a server rejected our optional extras (400/422) and the
    /// request was retried without them.
    int extraOptionsRejectedCount() const { return m_extraRejected; }

    /// The sentences the user reads, as pure functions. What a misconfigured
    /// endpoint *is* is the feature under test, and it must be testable without
    /// a socket, a timeout or a race: "HTTP status 0" is what the old build
    /// printed for a server that was simply not running.
    static QString httpFailureMessage(int status, const QString &reasonPhrase,
                                      const QUrl &url);
    static QString transportFailureMessage(QNetworkReply::NetworkError error,
                                           const QString &errorString,
                                           const QUrl &url);
    /// 200 OK that carried no completion text. `contentType` decides whether
    /// this reads as "the model said nothing" or "that is an HTML page, not an
    /// API" - a proxy login page used to be reported as an empty translation,
    /// pointing the user at the model instead of at the URL.
    static QString emptyCompletionMessage(const QUrl &url,
                                          const QString &contentType);

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

    void sendInternal(const ChatRequest &req, bool allowExtras);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    class SseParser *m_parser = nullptr;
    QString m_streamError;   // server-sent error seen mid-stream
    ChatRequest m_pending;   // as the caller asked for, for the extras retry
    bool m_usedExtras = false;
    int m_extraRejected = 0;
    bool m_sawContent = false;   // any non-empty delta in this reply?
};

} // namespace lt
