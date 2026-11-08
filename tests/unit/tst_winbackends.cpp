// The Windows half of the clipboard knob. There is one reader on this OS, so
// the answer to TRADUKU_CLIPBOARD=wl is a sentence - and the sentence has to
// exist and be tested, because a knob that is silently ignored is how a user
// ends up believing a clipboard works when nobody ever asked it to.

#include <QtTest>

#include "platform/win/WinClipboardPolicy.h"

using namespace lt;

class TestWinBackends : public QObject
{
    Q_OBJECT

private slots:
    void nothingForcedIsFine()
    {
        QCOMPARE(windowsClipboardProblem(QString()), QString());
        QCOMPARE(windowsClipboardProblem(QStringLiteral("qt")), QString());
        QCOMPARE(windowsClipboardProblem(QStringLiteral(" QT ")), QString());
    }

    void wlIsRefusedWithTheReason()
    {
        const QString s = windowsClipboardProblem(QStringLiteral("wl"));
        QVERIFY2(!s.isEmpty(), "the knob was ignored silently");
        QVERIFY2(s.contains(QStringLiteral("wl-paste")), qPrintable(s));
        // ... and not a generic "unsupported": the user is holding a Linux
        // troubleshooting note, and the answer has to say why it does not apply.
        QVERIFY2(s.contains(QStringLiteral("compositor")), qPrintable(s));
    }

    void x11IsRefusedWithTheReason()
    {
        const QString s = windowsClipboardProblem(QStringLiteral("X11"));
        QVERIFY2(!s.isEmpty(), "the knob was ignored silently");
        QVERIFY2(s.contains(QStringLiteral("X server")), qPrintable(s));
    }

    void anUnknownNameNamesTheChoices()
    {
        const QString s = windowsClipboardProblem(QStringLiteral("klipper"));
        QVERIFY2(s.contains(QStringLiteral("klipper")),
                 qPrintable(QStringLiteral("the refused value is not echoed: ") + s));
        QVERIFY2(s.contains(QStringLiteral("qt")), qPrintable(s));
    }
};

QTEST_GUILESS_MAIN(TestWinBackends)
#include "tst_winbackends.moc"
