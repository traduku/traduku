/*
    SelfTest must exercise the endpoint the user configured - including when
    --config (or $TRADUKU_CONFIG) points somewhere else. It used to construct
    Settings with the default path, so `--config X --selftest --live` tested a
    different endpoint than the one X described.
*/
#include <QtTest>
#include <QTextStream>
#include <QDir>
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

        // Ask the machine whether the poison worked, before believing it.
        // On Unix, QDir::tempPath() does not stop at a poisoned TMPDIR: it
        // answers the empty string, and an empty temp path means writes land in
        // the current working directory - which the root user of a container
        // can write. Measured in traduku-deb-builder:trixie: the settings row of SelfTest printed
        // "PASS  settings: defaults" against that fallback, so the row was
        // green while describing a machine that does not exist - the kind of
        // green that is never read again. Windows has no such fallback for
        // this pair (TMP/TEMP are the answer), which is why the row was green
        // there for the right reason and red here for the wrong one.
        QString afterPoison = QDir::tempPath();
        const QString answered = afterPoison;
        while (afterPoison.endsWith(QLatin1Char('/'))
               || afterPoison.endsWith(QLatin1Char('\\'))) {
            afterPoison.chop(1);
        }
        if (afterPoison != QString::fromUtf8(poisoned)) {
            qputenv("TMP", savedTmp);
            qputenv("TEMP", savedTemp);
            qputenv("TMPDIR", savedTmpdir);
            // Measured in the container: QDir::tempPath() answers the empty
            // string here, not "/tmp" and not "/" - a path with no name, which
            // is why the first version of this message printed nothing between
            // its words. An empty temp directory is not "no temp directory":
            // relative paths then land in the cwd, and root can write that.
            const QString reach = answered.isEmpty()
                ? QStringLiteral("empty (Qt found no temp directory, so writes "
                                 "go to the working directory)")
                : (answered == QLatin1String("/")
                   ? QStringLiteral("the filesystem root (/)") : answered);
            QSKIP(qPrintable(QStringLiteral(
                       "the poison did not build the machine this row is about: "
                       "Qt's temp directory is ") + reach
                   + QStringLiteral(", which this user can write")));
        }

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
