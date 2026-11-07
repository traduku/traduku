#include <QtTest>
#include <QDir>
#include <QFile>

#include "platform/qt/XdgAutostart.h"

using namespace lt;

// XDG autostart works on KDE, GNOME and any XDG-compliant desktop.
class TestXdgAutostart : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    /// A directory shaped like /etc/xdg/autostart, holding the entry the
    /// traduku-gnome package installs. Returned path is passed to XdgAutostart
    /// as a system dir; the real /etc is never written by these tests.
    QString installSystemEntry()
    {
        // An XDG_CONFIG_DIRS entry: the autostart/ subdirectory is the
        // class's business, not the caller's (that is how /etc/xdg works).
        const QString dir = m_dir.path() + "/etc-xdg";
        QDir().mkpath(dir + "/autostart");
        QFile f(dir + "/autostart/traduku.desktop");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return {};
        }
        f.write("[Desktop Entry]\nType=Application\nName=Traduku\n"
                "Exec=/usr/bin/traduku\n");
        f.close();
        return dir;
    }

private slots:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", (m_dir.path() + "/config").toUtf8());
    }

    void disabledByDefault()
    {
        XdgAutostart a(QStringLiteral("traduku"),
                       QStringLiteral("/usr/bin/traduku"));
        QVERIFY(!a.isEnabled());
    }

    void enablingWritesDesktopEntry()
    {
        XdgAutostart a(QStringLiteral("traduku"),
                       QStringLiteral("/usr/bin/traduku"));
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.isEnabled());
        QFile f(QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
                    .filePath(QStringLiteral("autostart/traduku.desktop")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString data = QString::fromUtf8(f.readAll());
        QVERIFY(data.contains(QStringLiteral("[Desktop Entry]")));
        QVERIFY(data.contains(QStringLiteral("Exec=/usr/bin/traduku")));
        QVERIFY(data.contains(QStringLiteral("Type=Application")));
    }

    void disablingRemovesIt()
    {
        XdgAutostart a(QStringLiteral("traduku"), QStringLiteral("/x"));
        a.setEnabled(true);
        QVERIFY(a.setEnabled(false));
        QVERIFY(!a.isEnabled());
    }

    void idempotent()
    {
        XdgAutostart a(QStringLiteral("traduku"), QStringLiteral("/x"));
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.isEnabled());
        a.setEnabled(false);
        a.setEnabled(false);
        QVERIFY(!a.isEnabled());
    }

    // ---- an entry the PACKAGE installed ---------------------------------
    // traduku-gnome ships /etc/xdg/autostart/traduku.desktop, so on GNOME the
    // app starts at login whatever the user ticked. "Launch on Login" read
    // "off" and un-ticking it removed a file that was not the reason the app
    // started: the control did nothing except look like it did. Per the
    // desktop-entry spec the answer is a same-named user file that shadows the
    // system one, so both directions have to be measured against a system dir.

    void aSystemEntryStartsTheAppWithoutAUserFile()
    {
        const QString dir = installSystemEntry();
        QVERIFY(!dir.isEmpty());
        XdgAutostart a(QStringLiteral("traduku"), QStringLiteral("/x"), {dir});
        QVERIFY2(a.isEnabled(),
                 "the package installed the entry; the menu must not show it off - "
                 "a control whose state contradicts the machine cannot be trusted "
                 "when it is right");
    }

    void turningItOffOverridesTheSystemEntry()
    {
        const QString dir = installSystemEntry();
        XdgAutostart a(QStringLiteral("traduku"), QStringLiteral("/x"), {dir});
        QVERIFY(a.isEnabled());
        QVERIFY(a.setEnabled(false));
        QVERIFY2(!a.isEnabled(),
                 "unchecking did not disable autostart: deleting the user file "
                 "cannot disable an entry the package owns");
        QFile f(QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
                    .filePath(QStringLiteral("autostart/traduku.desktop")));
        QVERIFY2(f.open(QIODevice::ReadOnly),
                 "disabling a system entry has to leave a shadowing user file "
                 "behind (Hidden=true), otherwise the next login starts the app again");
        const QString data = QString::fromUtf8(f.readAll());
        QVERIFY2(data.contains(QStringLiteral("Hidden=true")),
                 qPrintable(QStringLiteral("the override does not say Hidden=true: ") + data));
    }

    void turningItBackOnClearsTheOverride()
    {
        const QString dir = installSystemEntry();
        XdgAutostart a(QStringLiteral("traduku"), QStringLiteral("/x"), {dir});
        QVERIFY(a.setEnabled(false));
        QVERIFY(a.setEnabled(true));
        QVERIFY(a.isEnabled());
        QFile f(QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
                    .filePath(QStringLiteral("autostart/traduku.desktop")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString data = QString::fromUtf8(f.readAll());
        QVERIFY2(!data.contains(QStringLiteral("Hidden=true")),
                 "re-enabling left the override in place - the app would still not "
                 "start while the menu says it will");
        QVERIFY(data.contains(QStringLiteral("Exec=/x")));
    }

    void whatTheGnomeStartupUiWroteIsUnderstood()
    {
        // gnome-session's own "Startup Applications" tool (and gnome-control-
        // center's Apps panel) disables an entry by writing
        // X-GNOME-Autostart-enabled=false, not Hidden=true. A user who turned
        // Traduku off there must not see it ticked on in our menu.
        const QString dir = installSystemEntry();
        XdgAutostart a(QStringLiteral("traduku"), QStringLiteral("/x"), {dir});
        QVERIFY(a.setEnabled(false));
        QFile f(QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
                    .filePath(QStringLiteral("autostart/traduku.desktop")));
        QVERIFY(f.open(QIODevice::Append));
        f.write("X-GNOME-Autostart-enabled=false\n");
        f.close();
        QVERIFY(!a.isEnabled());
    }

    void theSystemEntryIsNeverRewritten()
    {
        // The user's choice is ours to record; the packaged file belongs to the
        // package manager. Rewriting /etc/xdg would be silently undone by the
        // next upgrade and is not ours to touch at all.
        const QString dir = installSystemEntry();
        QFile sys(dir + "/autostart/traduku.desktop");
        QVERIFY(sys.open(QIODevice::ReadOnly));
        const QByteArray before = sys.readAll();
        sys.close();

        XdgAutostart a(QStringLiteral("traduku"), QStringLiteral("/x"), {dir});
        QVERIFY(a.setEnabled(false));
        QVERIFY(a.setEnabled(true));
        QFile after(dir + "/autostart/traduku.desktop");
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before);
    }
};

QTEST_GUILESS_MAIN(TestXdgAutostart)
#include "tst_xdgautostart.moc"
