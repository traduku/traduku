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
    /// A machine with nowhere to write must not be reported as a broken app.
    ///
    /// The row exists because of what it caught in a portable package: the
    /// selftest's settings row writes into a QTemporaryDir, and with no usable
    /// TMP/TEMP that write fails - and the report said `FAIL  settings:
    /// save/reload round-trip`, blaming the persistence code for a machine that
    /// had no temp directory. The failure named the wrong suspect, which is
    /// worse than silence: a user goes looking in the config code.
    void anUnusableTempDirIsSkippedWithItsReasonAndNotBlamedOnSettings()
    {
        QTemporaryDir blocker;
        QVERIFY(blocker.isValid());
        // A directory under a plain file cannot be created on any filesystem
        // we ship on, which makes this a portable way to say "no temp dir".
        QFile blockerFile(blocker.path() + QStringLiteral("/file"));
        QVERIFY(blockerFile.open(QIODevice::WriteOnly));

        const QByteArray savedTmp = qgetenv("TMP");
        const QByteArray savedTemp = qgetenv("TEMP");
        const QByteArray savedTmpdir = qgetenv("TMPDIR");
        const QByteArray poisoned =
            (blockerFile.fileName() + QStringLiteral("/nope")).toUtf8();
        qputenv("TMP", poisoned);
        qputenv("TEMP", poisoned);
        qputenv("TMPDIR", poisoned);

        QString log;
        QTextStream out(&log);
        runSelfTest(out, false, QString());

        qputenv("TMP", savedTmp);
        qputenv("TEMP", savedTemp);
        qputenv("TMPDIR", savedTmpdir);

        QVERIFY2(!log.contains(QStringLiteral("FAIL  settings")),
                 qPrintable(QStringLiteral("settings blamed for a machine with "
                                           "no temp directory:\n") + log));
        QVERIFY2(log.contains(QStringLiteral("SKIP  settings")),
                 qPrintable(QStringLiteral("the row disappeared instead of being "
                                           "skipped with a reason:\n") + log));
        // ... and the reason is the thing the user can act on.
        QVERIFY2(log.contains(QStringLiteral("tempor"), Qt::CaseInsensitive),
                 qPrintable(log));
    }
};

QTEST_MAIN(TestSelfTest)
#include "tst_selftest.moc"
