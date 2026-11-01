#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace lt {

/*!
 * Incremental parser for the OpenAI-compatible SSE stream
 * ("text/event-stream" chunks of POST /chat/completions with stream=true).
 *
 * Feed arbitrary byte fragments via append(); complete "data:" payloads
 * become content deltas retrievable with takeDeltas(). A `data: [DONE]`
 * line or a `data: {"error":...}` payload marks isDone(); errors surface
 * through takeError(). Lines are consumed at '\\n' boundaries, so splitting
 * a multibyte UTF-8 sequence across appends is safe (JSON decoding happens
 * on the reassembled, complete line).
 */
class SseParser
{
public:
    void append(const QByteArray &bytes);

    /// Content deltas accumulated since the last call (cleared by it).
    QVector<QString> takeDeltas();
    /// First server-sent error message (cleared by the call), empty if none.
    QString takeError();
    /// Count of data lines that failed to parse (diagnostics).
    int takeErrorsSkipped();

    bool isDone() const { return m_done; }
    /// Last seen choices[0].finish_reason ("stop", "length", ...) - empty
    /// when the stream gave none. "length" means truncated generation.
    QString finishReason() const { return m_finishReason; }

private:
    void handleLine(const QByteArray &line);
    void handleData(const QByteArray &payload);

    QByteArray m_buffer;
    QVector<QString> m_deltas;
    QString m_error;
    int m_skipped = 0;
    bool m_done = false;
    QString m_finishReason;
};

} // namespace lt
