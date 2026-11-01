#include "sseparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lt {

void SseParser::append(const QByteArray &bytes)
{
    m_buffer += bytes;
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        handleLine(line);
    }
}

void SseParser::handleLine(const QByteArray &line)
{
    if (line.isEmpty() || line.startsWith(':')) {   // blank / comment
        return;
    }
    if (!line.startsWith("data:")) {                // event:, id:, retry:
        return;
    }
    handleData(line.mid(5).trimmed());
}

void SseParser::handleData(const QByteArray &payload)
{
    if (payload.isEmpty()) {
        return;
    }
    if (payload == QByteArrayLiteral("[DONE]")) {
        m_done = true;
        return;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        ++m_skipped;
        return;
    }
    const QJsonObject obj = doc.object();

    const QJsonObject err = obj.value(QLatin1String("error")).toObject();
    if (!err.isEmpty()) {
        m_error = err.value(QLatin1String("message")).toString();
        if (m_error.isEmpty()) {
            m_error = QString::fromUtf8(payload);
        }
        m_done = true;
        return;
    }

    const QJsonArray choices = obj.value(QLatin1String("choices")).toArray();
    if (!choices.isEmpty()) {
        const QJsonObject choice = choices.at(0).toObject();
        const QString fr = choice.value(QLatin1String("finish_reason")).toString();
        if (!fr.isEmpty()) {
            m_finishReason = fr;
        }
        const QString content = choice.value(QLatin1String("delta")).toObject()
                                  .value(QLatin1String("content")).toString();
        if (!content.isEmpty()) {
            m_deltas.append(content);
        }
    }
}

QVector<QString> SseParser::takeDeltas()
{
    QVector<QString> out;
    out.swap(m_deltas);
    return out;
}

QString SseParser::takeError()
{
    QString out;
    out.swap(m_error);
    return out;
}

int SseParser::takeErrorsSkipped()
{
    const int n = m_skipped;
    m_skipped = 0;
    return n;
}

} // namespace lt
