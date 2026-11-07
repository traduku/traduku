#include <QtTest>
#include <QTemporaryDir>

#include "app/cli.h"
#include "core/settings.h"
#include "mocksseserver.h"

using namespace lt;

// `traduku --translate --text ...` is the mode the test suite and any
// script use; it must not depend on a tray, a notifier or a clipboard.
class TestOneShot : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    test::MockSseServer m_server;
    Settings *m_settings = nullptr;

private slots:
    void init()
    {
        m_server.setEchoMode(true);
        m_settings = new Settings(m_dir.filePath(QStringLiteral("oneshot%1.ini")
                                                     .arg(m_server.requestCount())), this);
        m_settings->setApiUrl(m_server.baseUrl());
        m_settings->setModel(QStringLiteral("m"));
        m_settings->setTargetLanguage(QStringLiteral("ru"));
    }
    void cleanup() { delete m_settings; m_settings = nullptr; }

    void streamsDeltasAndSucceeds()
    {
        QStringList deltas;
        int code = -1;
        QString error;
        bool done = false;
        runOneShotTranslation(m_settings, QStringLiteral("hello world"),
                              [&](const QString &chunk) { deltas << chunk; },
                              [&](int rc, const QString &e) {
                                  code = rc; error = e; done = true;
                              });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QCOMPARE(code, 0);
        QVERIFY2(deltas.size() >= 2,
                 "the result must arrive as a stream, not one blob");
        QCOMPARE(deltas.join(QString()).trimmed(), QStringLiteral("HELLO WORLD"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void emptyTextNeverTouchesTheNetwork()
    {
        const int before = m_server.requestCount();
        int code = -1;
        QString error;
        bool done = false;
        runOneShotTranslation(m_settings, QStringLiteral("   "), {},
                              [&](int rc, const QString &e) {
                                  code = rc; error = e; done = true;
                              });
        QVERIFY(done);                       // must be synchronous
        QCOMPARE(code, 1);
        QCOMPARE(m_server.requestCount(), before);
        QVERIFY2(error.contains(QStringLiteral("empty"), Qt::CaseInsensitive),
                 qPrintable(error));
    }

    void serverFailureBecomesExitCodeAndMessage()
    {
        // A gateway HTML page - no JSON error for the SSE parser to find,
        // so the HTTP status itself is all the operator gets.
        m_server.setEchoMode(false);
        m_server.setResponse(502, {QByteArray("<html><body>Bad Gateway</body></html>")});
        int code = -1;
        QString error;
        bool done = false;
        runOneShotTranslation(m_settings, QStringLiteral("anything"), {},
                              [&](int rc, const QString &e) {
                                  code = rc; error = e; done = true;
                              });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QCOMPARE(code, 1);
        QVERIFY2(!error.isEmpty(), "the operator gets no reason for the failure");
        // "something failed" is not a reason. The status and the address are
        // what turns a support question into a two-second fix.
        QVERIFY2(error.contains(QStringLiteral("502")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("127.0.0.1")), qPrintable(error));
    }

    void deadEndpointIsNamedNotShruggedOff()
    {
        // The configuration error people actually make: the server is not
        // running, or the port is wrong. This used to come out as
        // "… (HTTP status 0)".
        Settings dead(m_dir.filePath(QStringLiteral("dead.ini")), this);
        dead.setApiUrl(QStringLiteral("http://127.0.0.1:1/v1"));   // nothing there
        dead.setModel(QStringLiteral("m"));
        dead.setTargetLanguage(QStringLiteral("ru"));
        int code = -1;
        QString error;
        bool done = false;
        runOneShotTranslation(&dead, QStringLiteral("anything"), {},
                              [&](int rc, const QString &e) {
                                  code = rc; error = e; done = true;
                              });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QCOMPARE(code, 1);
        QVERIFY2(error.contains(QStringLiteral("127.0.0.1")), qPrintable(error));
        QVERIFY2(!error.trimmed().startsWith(QStringLiteral("HTTP status 0")),
                 qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("listening"), Qt::CaseInsensitive)
                     || error.contains(QStringLiteral("refused"), Qt::CaseInsensitive),
                 qPrintable(error));
    }

    void amodelTheServerDoesNotKnowIsNotReportedAsAnEmptyTranslation()
    {
        // 200 OK, no text: the shape a wrong model name or a proxy page makes.
        m_server.setEchoMode(false);
        m_server.setResponse(200, {QByteArray("data: [DONE]\n\n")});
        int code = -1;
        QString error;
        bool done = false;
        runOneShotTranslation(m_settings, QStringLiteral("anything"), {},
                              [&](int rc, const QString &e) {
                                  code = rc; error = e; done = true;
                              });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QCOMPARE(code, 1);
        QVERIFY2(error.contains(QStringLiteral("text"), Qt::CaseInsensitive)
                     || error.contains(QStringLiteral("empty"), Qt::CaseInsensitive),
                 qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("model"), Qt::CaseInsensitive),
                 qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(TestOneShot)
#include "tst_oneshot.moc"
