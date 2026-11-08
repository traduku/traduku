// What --backend-info says about the session, on the one OS where "session"
// also means "is there a desktop at all here". The formatting is a pure
// function of facts, so the facts can be written by the test - which is the
// only way to see the sentence a service running in session 0 would print,
// because a test cannot be in session 0.

#include <QtTest>

#include "platform/win/WinSessionInfo.h"

using namespace lt;

namespace {

WinSessionFacts facts(bool interactive, bool remote, quint32 id = 1,
                      const QString &desktop = QStringLiteral("Default"))
{
    WinSessionFacts f;
    f.interactiveDesktop = interactive;
    f.remoteSession = remote;
    f.sessionId = id;
    f.desktopName = desktop;
    return f;
}

} // namespace

class TestWinSessionInfo : public QObject
{
    Q_OBJECT

private slots:
    void interactiveConsoleIsJustWindows()
    {
        const QString s = winSessionString(facts(true, false));
        QVERIFY2(s.startsWith(QStringLiteral("windows")), qPrintable(s));
        QVERIFY2(!s.contains(QStringLiteral("no ")),
                 qPrintable(QStringLiteral("advice where nothing is wrong: ") + s));
    }

    void aRemoteSessionIsNamedBecauseItChangesLatencyAndClipboards()
    {
        QVERIFY(winSessionString(facts(true, true)).contains(QStringLiteral("remote")));
    }

    void withoutAnInteractiveDesktopItSaysWhatCannotWork()
    {
        // A service, a scheduled task in session 0, or `traduku` under a
        // non-interactive logon: the gesture has no keyboard to observe, and
        // the report must not leave the user deducing that from a missing
        // tray icon.
        const QString s = winSessionString(facts(false, false));
        QVERIFY2(s.contains(QStringLiteral("no interactive desktop")), qPrintable(s));
        QVERIFY2(s.contains(QStringLiteral("gesture")),
                 qPrintable(QStringLiteral("names the consequence, not only the fact: ")
                            + s));
    }

    void theDesktopNameIsPassedThroughWhenThereIsOne()
    {
        QVERIFY(winSessionString(facts(true, false, 7, QStringLiteral("Winsta0\\Default")))
                    .contains(QStringLiteral("Winsta0\\Default")));
    }

    void sessionIsNeverReportedAsTheQpaPluginName()
    {
        // The Linux rule, kept here: QGuiApplication::platformName() is
        // "windows" or "offscreen", and offscreen must not become the answer
        // to "what session am I in" (SessionInfo.h).
        QVERIFY(!winSessionString(facts(true, false)).contains(QStringLiteral("offscreen")));
        WinSessionFacts f = facts(true, false);
        f.qpaName = QStringLiteral("offscreen");
        QVERIFY2(!winSessionString(f).startsWith(QStringLiteral("offscreen")),
                 qPrintable(winSessionString(f)));
    }
};

QTEST_GUILESS_MAIN(TestWinSessionInfo)
#include "tst_winsessioninfo.moc"
