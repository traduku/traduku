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
    void setResponse(int status, const QList<QByteArray> &bodyParts)
    {
        m_status = status;
        m_bodyParts = bodyParts;
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

    int requestCount() const { return m_requests.size(); }

    QList<QByteArray> m_requests;   // full request bytewise, in arrival order

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        sock->setSocketDescriptor(socketDescriptor);
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
            m_requests.append(m_pending);
            const QByteArray req = m_pending;
            m_pending.clear();
            if (m_echo) {
                respondEcho(sock, req);
            } else {
                respond(sock);
            }
        });
    }

private:
    void respond(QTcpSocket *sock)
    {
        QByteArray head = QStringLiteral("HTTP/1.1 %1 MOCK\r\n"
                                         "Content-Type: text/event-stream\r\n"
                                         "Connection: close\r\n\r\n")
                              .arg(m_status)
                              .toUtf8();
        sock->write(head);
        for (const QByteArray &part : m_bodyParts) {
            sock->write(part);
            sock->flush();
        }
        sock->disconnectFromHost();
    }

    void respondEcho(QTcpSocket *sock, const QByteArray &request)
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
            sock->write(sseEvent(w.toUpper() + QLatin1Char(' ')));
            sock->flush();
        }
        sock->write(sseEvent(text.toUpper().isEmpty()
                                 ? QString() : QStringLiteral("")));
        sock->write("data: [DONE]\n\n");
        sock->flush();
        sock->disconnectFromHost();
    }

    bool m_echo = false;
    int m_status = 200;
    QList<QByteArray> m_bodyParts;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace test
} // namespace lt
