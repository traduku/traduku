#pragma once
// In-process HTTP mock for OpenAI-compatible /chat/completions, used by the
// integration tests. Serves one canned response per accepted connection in
// arbitrary byte fragments (flush after each), so streaming clients must do
// real incremental parsing. Single-threaded, event-loop only.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>

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

    QList<QByteArray> m_requests;   // full request bytewise, in arrival order

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        sock->setSocketDescriptor(socketDescriptor);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
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
            m_pending.clear();
            respond(sock);
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

    int m_status = 200;
    QList<QByteArray> m_bodyParts;
    QByteArray m_pending;
};

} // namespace test
} // namespace lt
