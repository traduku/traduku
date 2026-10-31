#include <QtTest>
#include <QFile>

#include "platform/qt/XdgAutostart.h"

using namespace lt;

// XDG autostart works on KDE, GNOME and any XDG-compliant desktop.
class TestXdgAutostart : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", (m_dir.path() + "/config").toUtf8());
    }

    void disabledByDefault()
    {
        XdgAutostart a(QStringLiteral("llm-translate"),
                       QStringLiteral("/usr/bin/llm-translate"));
        QVERIFY(!a.isEnabled());
    }

    void enablingWritesDesktopEntry()
    {
        XdgAutostart a(QStringLiteral("llm-translate"),
                       QStringLiteral("/usr/bin/llm-translate"));
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.isEnabled());
        QFile f(QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
                    .filePath(QStringLiteral("autostart/llm-translate.desktop")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString data = QString::fromUtf8(f.readAll());
        QVERIFY(data.contains(QStringLiteral("[Desktop Entry]")));
        QVERIFY(data.contains(QStringLiteral("Exec=/usr/bin/llm-translate")));
        QVERIFY(data.contains(QStringLiteral("Type=Application")));
    }

    void disablingRemovesIt()
    {
        XdgAutostart a(QStringLiteral("llm-translate"), QStringLiteral("/x"));
        a.setEnabled(true);
        QVERIFY(a.setEnabled(false));
        QVERIFY(!a.isEnabled());
    }

    void idempotent()
    {
        XdgAutostart a(QStringLiteral("llm-translate"), QStringLiteral("/x"));
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.isEnabled());
        a.setEnabled(false);
        a.setEnabled(false);
        QVERIFY(!a.isEnabled());
    }
};

QTEST_GUILESS_MAIN(TestXdgAutostart)
#include "tst_xdgautostart.moc"
