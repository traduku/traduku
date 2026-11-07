/*
    Which tray a build shows, and how the operator overrides it for a run.

    There used to be two implementations to compare (KStatusNotifierItem and
    QSystemTrayIcon). The KDE one is gone - it segfaulted on destruction with a
    live session bus (pinned by a coredump of the released 0.2 build), and the
    Qt tray matched it item for item in Plasma - so the only tray left is the
    Qt one. The switch and this contract stay, because:

      * a packaging default is still worth having (LT_TRAY=none for a build
        that ships without a panel icon), and
      * people have old instructions, and `TRADUKU_TRAY=kde` must say *why* it
        does nothing instead of silently ignoring them.
*/
#include <QtTest>

#include "app/TrayChoice.h"

using namespace lt;

class TestTrayChoice : public QObject
{
    Q_OBJECT
private slots:
    void buildDefaultWinsWithoutOverride()
    {
        const TrayChoice c = resolveTrayKind(QStringLiteral("qt"), QString());
        QCOMPARE(c.kind, QStringLiteral("qt"));
        QCOMPARE(c.source, QStringLiteral("built-in default"));
    }

    void emptyEnvIsNotAnOverride()
    {
        QCOMPARE(resolveTrayKind(QStringLiteral("qt"), QString()).kind,
                 QStringLiteral("qt"));
        QCOMPARE(resolveTrayKind(QStringLiteral("qt"), QStringLiteral("")).kind,
                 QStringLiteral("qt"));
    }

    void envSelectsNoTray()
    {
        // A kiosk or a --translate-only deployment: no panel icon at all.
        const TrayChoice c = resolveTrayKind(QStringLiteral("qt"),
                                             QStringLiteral("none"));
        QCOMPARE(c.kind, QStringLiteral("none"));
        QCOMPARE(c.source, QStringLiteral("TRADUKU_TRAY"));
    }

    void envIsCaseInsensitiveAndTrimmed()
    {
        // Typed at a terminal: " NONE ", "None" must work.
        QCOMPARE(resolveTrayKind(QStringLiteral("qt"),
                                QStringLiteral(" NONE ")).kind,
                 QStringLiteral("none"));
        QCOMPARE(resolveTrayKind(QStringLiteral("none"),
                                QStringLiteral("Qt")).kind,
                 QStringLiteral("qt"));
    }

    void theRemovedKdeTrayIsExplainedNotIgnored()
    {
        // `TRADUKU_TRAY=kde` is in old READMEs and old muscle memory. It must
        // not fall on the floor silently: the operator would think they were
        // looking at KStatusNotifierItem.
        const TrayChoice c = resolveTrayKind(QStringLiteral("qt"),
                                             QStringLiteral("kde"));
        QCOMPARE(c.kind, QStringLiteral("qt"));         // still usable
        QVERIFY(!c.warning.isEmpty());
        QVERIFY(c.warning.contains(QStringLiteral("kde")));
        QVERIFY(c.warning.contains(QStringLiteral("removed"))
                || c.warning.contains(QStringLiteral("no longer")));
    }

    void anUnknownValueIsLoudNotSilent()
    {
        // A typo must not quietly show something else: the operator would
        // compare the wrong thing and report on it.
        const TrayChoice c = resolveTrayKind(QStringLiteral("qt"),
                                             QStringLiteral("qyt"));
        QCOMPARE(c.kind, QStringLiteral("qt"));         // still usable
        QVERIFY(!c.warning.isEmpty());
        QVERIFY(c.warning.contains(QStringLiteral("qyt")));   // says what it saw
        QVERIFY(c.warning.contains(QStringLiteral("qt")));    // says what to type
    }

    void displayNameIsNeverEmpty()
    {
        // --backend-info prints this; an empty cell reads as "no tray".
        QVERIFY(!trayDisplayName(QStringLiteral("qt")).isEmpty());
        QVERIFY(!trayDisplayName(QStringLiteral("none")).isEmpty());
        QVERIFY(!trayDisplayName(QStringLiteral("wat?")).isEmpty());
        QVERIFY(trayDisplayName(QStringLiteral("qt"))
                .contains(QStringLiteral("QSystemTrayIcon")));
        QVERIFY(trayDisplayName(QStringLiteral("none"))
                .contains(QStringLiteral("no tray")));
    }
};

QTEST_GUILESS_MAIN(TestTrayChoice)
#include "tst_traychoice.moc"
