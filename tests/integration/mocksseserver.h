#pragma once
// In-process HTTP mock for OpenAI-compatible /chat/completions, used by the
// integration tests. Serves one canned response per accepted connection in
// arbitrary byte fragments (flush after each), so streaming clients must do
// real incremental parsing. Single-threaded, event-loop only.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>

namespace lt {
namespace test {

class MockSseServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit MockSseServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const   // e.g. http://127.0.0.1:PORT/v1
    {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort());
    }

    /// Response for the next connection: status line + raw body fragments.
    /// `contentType` is a lie detector: a proxy login page answers 200 with
    /// text/html, and the client must not read that as "the model returned
    /// nothing".
    void setResponse(int status, const QList<QByteArray> &bodyParts,
                     const QByteArray &contentType = QByteArray("text/event-stream"))
    {
        m_status = status;
        m_bodyParts = bodyParts;
        m_contentType = contentType;
    }

    /// SSE helpers -------------------------------------------------------
    static QByteArray sseEvent(const QString &content)
    {
        const QJsonObject obj{
            {QStringLiteral("choices"),
             QJsonArray{QJsonObject{{QStringLiteral("delta"),
                                     QJsonObject{{QStringLiteral("content"), content}}}}}}};
        return QByteArrayLiteral("data: ")
            + QJsonDocument(obj).toJson(QJsonDocument::Compact)
            + "\n\n";
    }

    /// Echo mode: reply with the text following "### Original text" in the
    /// request, uppercased, streamed word by word.
    void setEchoMode(bool on) { m_echo = on; }

    /// First request only: echo the first `words` words, then end the
    /// stream with finish_reason == "length" (output-limit truncation).
    void setTruncateFirstRequest(int words) { m_truncateFirst = words; }

    /// Echo collapses all line breaks into spaces (structure-destroying
    /// model behaviour -> alignment must fail -> chunk fallback).
    void setEchoSquash(bool on) { m_squash = on; }

    /// One server object is shared by every slot of a test class, so slots
    /// must not inherit each other's behaviour.
    void reset()
    {
        // A slot that closed the server to test "nothing is listening" must not
        // leave every later slot connecting to port 0 and reporting the
        // connection-refused path instead of the one it means to test.
        if (!isListening()) {
            listen(QHostAddress::LocalHost, 0);
        }
        m_echo = false;
        m_squash = false;
        m_truncateFirst = 0;
        m_failNext = 0;
        m_status = 200;
        m_contentType = QByteArray("text/event-stream");
        m_bodyParts.clear();
        m_requests.clear();
    }

    /// Answer the next `n` requests with `status` and a JSON error body -
    /// a strict gateway rejecting options it does not know.
    void setFailNextRequests(int n, int status = 400)
    {
        m_failNext = n;
        m_failStatus = status;
    }

    int requestCount() const { return m_requests.size(); }

    QList<QByteArray> m_requests;   // full request bytewise, in arrival order

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        sock->setSocketDescriptor(socketDescriptor);
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
            m_buffers.remove(sock);
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            QByteArray &m_pending = m_buffers[sock];
            m_pending += sock->readAll();
            const int headEnd = m_pending.indexOf("\r\n\r\n");
            if (headEnd < 0) {
                return;
            }
            const QByteArray head = m_pending.left(headEnd);
            const int clen = head.contains("content-length:")
                ? head.mid(head.indexOf("content-length:") + 15).trimmed()
                      .split('\r').first().toInt()
                : head.contains("Content-Length:")
                    ? head.mid(head.indexOf("Content-Length:") + 15).trimmed()
                          .split('\r').first().toInt()
                    : 0;
            if (m_pending.size() < headEnd + 4 + clen) {
                return;                       // body incomplete
            }
            const int reqNo = m_requests.size() + 1;
            m_requests.append(m_pending);
            const QByteArray req = m_pending;
            m_pending.clear();
            if (m_failNext > 0) {
                --m_failNext;
                respondError(sock, m_failStatus);
                return;
            }
            if (m_echo) {
                respondEcho(sock, req, m_requests.size() + 1);
            } else {
                respond(sock);
            }
        });
    }

private:
    void respondError(QTcpSocket *sock, int status)
    {
        const QByteArray body = "{\"error\":{\"message\":\"Unrecognized request argument\"}}";
        sock->write(QStringLiteral("HTTP/1.1 %1 REJECTED\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: %2\r\n"
                                  "Connection: close\r\n\r\n")
                         .arg(status).arg(body.size()).toUtf8());
        sock->write(body);
        sock->flush();
        sock->disconnectFromHost();
    }

    void respond(QTcpSocket *sock)
    {
        QByteArray head = QStringLiteral("HTTP/1.1 %1 MOCK\r\n"
                                         "Content-Type: %2\r\n"
                                         "Connection: close\r\n\r\n")
                              .arg(m_status)
                              .arg(QString::fromLatin1(m_contentType))
                              .toUtf8();
        sock->write(head);
        for (const QByteArray &part : m_bodyParts) {
            sock->write(part);
            sock->flush();
        }
        sock->disconnectFromHost();
    }

    void respondEcho(QTcpSocket *sock, const QByteArray &request, int reqNo)
    {
        QString text;
        const int p = request.indexOf("<<<TEXT");
        if (p >= 0) {
            const QByteArray body = request.mid(request.indexOf("\r\n\r\n") + 4);
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            const QString prompt = doc.object()
                .value(QStringLiteral("messages")).toArray()
                .at(0).toObject().value(QStringLiteral("content")).toString();
            const int s = prompt.indexOf(QStringLiteral("<<<TEXT"));
            if (s >= 0) {
                text = prompt.mid(s + 7);
                const int stop = text.lastIndexOf(QStringLiteral("TEXT"));
                if (stop >= 0) {
                    text = text.left(stop);
                }
                text = text.trimmed();
            }
        }
        QByteArray head = QStringLiteral("HTTP/1.1 200 MOCK\r\n"
                                         "Content-Type: text/event-stream\r\n"
                                         "Connection: close\r\n\r\n").toUtf8();
        sock->write(head);
        const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &w : words) {
            QString piece = w.toUpper() + QLatin1Char(' ');
            if (m_squash) {
                piece.replace(QLatin1Char('\n'), QLatin1Char(' '));
            }
            sock->write(sseEvent(piece));
            sock->flush();
        }
        sock->write(sseEvent(text.toUpper().isEmpty()
                                 ? QString() : QStringLiteral("")));
        sock->write("data: [DONE]\n\n");
        sock->flush();
        sock->disconnectFromHost();
    }

    bool m_echo = false;
    int m_truncateFirst = 0;
    int m_failNext = 0;
    int m_failStatus = 400;
    bool m_squash = false;
    int m_status = 200;
    QByteArray m_contentType{"text/event-stream"};
    QList<QByteArray> m_bodyParts;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace test
} // namespace lt
