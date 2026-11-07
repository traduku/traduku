#include <QtTest>

#include "platform/qt/SessionInfo.h"

using namespace lt;

// Clipboard and gesture backends have to be chosen by the SESSION the user
// is in, not by the Qt plugin that happens to be loaded: CI and
// `--selftest` force QT_QPA_PLATFORM=offscreen, which would otherwise make
// a Wayland box look like it has no Wayland at all (and silently pick the
// Qt clipboard, which is blind there).
class TestSessionInfo : public QObject
{
    Q_OBJECT
private slots:
    void offscreenFallsBackToXdgSessionType()
    {
        QVERIFY2(QGuiApplication::platformName() == QLatin1String("offscreen"),
                 "this suite is supposed to run offscreen (lt_add_test sets it)");
        qputenv("XDG_SESSION_TYPE", "wayland");
        QCOMPARE(sessionType(), QStringLiteral("wayland"));
        QVERIFY(isWaylandSession());

        qputenv("XDG_SESSION_TYPE", "x11");
        QCOMPARE(sessionType(), QStringLiteral("x11"));
        QVERIFY(!isWaylandSession());
    }

    void noInformationIsHonest()
    {
        qunsetenv("XDG_SESSION_TYPE");
        QVERIFY2(sessionType().isEmpty(),
                 qPrintable(QStringLiteral("invented a session: %1").arg(sessionType())));
        QVERIFY(!isWaylandSession());
    }

    void environmentWinsOverAnUnknownPlugin()
    {
        qputenv("XDG_SESSION_TYPE", "wayland");
        QCOMPARE(sessionType(), QStringLiteral("wayland"));
        qunsetenv("XDG_SESSION_TYPE");
    }
};

QTEST_MAIN(TestSessionInfo)
#include "tst_sessioninfo.moc"
