/*
    SelfTest must exercise the endpoint the user configured - including when
    --config (or $TRADUKU_CONFIG) points somewhere else. It used to construct
    Settings with the default path, so `--config X --selftest --live` tested a
    different endpoint than the one X described.
*/
#include <QtTest>
#include <QTextStream>
#include <QTemporaryDir>

#include "app/SelfTest.h"
#include "core/settings.h"
#include "mocksseserver.h"

using namespace lt;

class TestSelfTest : public QObject
{
    Q_OBJECT
    test::MockSseServer m_server;
    QTemporaryDir m_dir;

private slots:
    void liveCheckUsesTheConfigFileItWasGiven()
    {
        m_server.reset();
        m_server.setResponse(200, {test::MockSseServer::sseEvent("Bonjour"),
                                   QByteArray("data: [DONE]\n\n")});
        const QString path = m_dir.path() + QStringLiteral("/config.ini");
        {
            Settings s(path);
            s.setApiUrl(m_server.baseUrl());
            s.setModel(QStringLiteral("test-model"));
            s.save();
        }

        QString log;
        QTextStream out(&log);
        QCOMPARE(runSelfTest(out, true, path), 0);
        QVERIFY2(log.contains(QStringLiteral("live: translate via configured endpoint")),
                 qPrintable(log));
    }
};

QTEST_MAIN(TestSelfTest)
#include "tst_selftest.moc"
