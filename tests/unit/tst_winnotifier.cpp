// Windows notifications. There is no freedesktop notification service here and
// no KNotification; what Windows gives a plain desktop app without an installer
// identity is a balloon on a tray icon. So the notifier is thin - but "thin"
// is exactly where silent failure lives, and this product's rule is that a
// cue the user cannot hear must be reported by --backend-info rather than
// assumed (SoundPlayer::Status is the same idea for sound).

#include <QtTest>

#include "platform/win/WinNotifier.h"

using namespace lt;

namespace {

struct Sent
{
    QString title;
    QString body;
    int ms = -1;
};

} // namespace

class TestWinNotifier : public QObject
{
    Q_OBJECT

private slots:
    void notifyHandsTitleBodyAndTimeoutToTheSender()
    {
        QList<Sent> sent;
        WinNotifier n([&sent](const QString &title, const QString &body, int ms) {
            sent.append({title, body, ms});
            return true;
        });
        n.notify(QStringLiteral("Traduku"), QStringLiteral("Translation ready"),
                 QString(), 2500);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.at(0).title, QStringLiteral("Traduku"));
        QCOMPARE(sent.at(0).body, QStringLiteral("Translation ready"));
        QCOMPARE(sent.at(0).ms, 2500);
        QCOMPARE(n.delivered(), quint64(1));
        QCOMPARE(n.dropped(), quint64(0));
        QVERIFY(n.errorText().isEmpty());
    }

    void aRejectedBalloonIsADropWithAReason()
    {
        // The host can refuse (focus assist, no shell, a session with no
        // desktop). A notifier that "sent" it anyway is how --backend-info
        // ends up lying about a machine.
        WinNotifier n([](const QString &, const QString &, int) { return false; });
        n.notify(QStringLiteral("Traduku"), QStringLiteral("hello"), QString(), 0);
        QCOMPARE(n.delivered(), quint64(0));
        QCOMPARE(n.dropped(), quint64(1));
        QVERIFY2(!n.errorText().isEmpty(),
                 "a failed notification must leave a reason behind");
    }

    void withoutAnyChannelItSaysNoneInsteadOfPretending()
    {
        WinNotifier n(nullptr);
        n.notify(QStringLiteral("Traduku"), QStringLiteral("hello"), QString(), 0);
        QVERIFY2(n.backendName().contains(QStringLiteral("none")),
                 qPrintable(n.backendName()));
        QCOMPARE(n.dropped(), quint64(1));
        QVERIFY2(n.errorText().contains(QStringLiteral("tray")),
                 qPrintable(n.errorText()));
    }

    void backendNameNamesTheChannelThatWillActuallyBeUsed()
    {
        WinNotifier n([](const QString &, const QString &, int) { return true; });
        QVERIFY2(n.backendName().contains(QStringLiteral("balloon")),
                 qPrintable(n.backendName()));
        // The name is a claim about this machine, so it must not depend on a
        // call having happened yet: --backend-info prints it before notifying.
        QCOMPARE(n.delivered(), quint64(0));
    }

    void errorTextKeepsTheLastFailureNotTheLastSuccess()
    {
        bool ok = true;
        WinNotifier n([&ok](const QString &, const QString &, int) { return ok; });
        n.notify(QStringLiteral("a"), QStringLiteral("b"), QString(), 0);
        QVERIFY(n.errorText().isEmpty());
        ok = false;
        n.notify(QStringLiteral("a"), QStringLiteral("b"), QString(), 0);
        QVERIFY(!n.errorText().isEmpty());
        ok = true;
        n.notify(QStringLiteral("a"), QStringLiteral("b"), QString(), 0);
        // The recovery is real, and so is the history: a user reporting "no
        // notifications since lunch" needs the failure to still be printable.
        QVERIFY(!n.lastError().isEmpty());
        QCOMPARE(n.delivered(), quint64(2));
        QCOMPARE(n.dropped(), quint64(1));
    }
};

QTEST_GUILESS_MAIN(TestWinNotifier)
#include "tst_winnotifier.moc"
