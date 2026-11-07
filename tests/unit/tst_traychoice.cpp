/*
    Which tray a build shows, and how the operator overrides it for a run.

    Stage 6b is a comparison: the same traduku binary, first with KdeTray,
    then with the Qt one, in a live Plasma session. A compile-time switch
    alone would make that two builds, and "did you rebuild the right one?" is
    not a measurement. So LT_TRAY sets the build default (packaging) and
    TRADUKU_TRAY overrides it per run (measurement) - and this decision is
    pure, so it is tested instead of trusted.
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
        const TrayChoice c = resolveTrayKind(QStringLiteral("kde"), QString());
        QCOMPARE(c.kind, QStringLiteral("kde"));
        QCOMPARE(c.source, QStringLiteral("built-in default"));
    }

    void emptyEnvIsNotAnOverride()
    {
        QCOMPARE(resolveTrayKind(QStringLiteral("kde"), QString()).kind,
                 QStringLiteral("kde"));
        QCOMPARE(resolveTrayKind(QStringLiteral("kde"), QStringLiteral("")).kind,
                 QStringLiteral("kde"));
    }

    void envSelectsTheQtTray()
    {
        const TrayChoice c = resolveTrayKind(QStringLiteral("kde"),
                                             QStringLiteral("qt"));
        QCOMPARE(c.kind, QStringLiteral("qt"));
        QCOMPARE(c.source, QStringLiteral("TRADUKU_TRAY"));
    }

    void envIsCaseInsensitiveAndTrimmed()
    {
        // Typed at a terminal: " QT ", "Qt" must work.
        QCOMPARE(resolveTrayKind(QStringLiteral("kde"),
                                QStringLiteral(" QT ")).kind,
                 QStringLiteral("qt"));
        QCOMPARE(resolveTrayKind(QStringLiteral("qt"),
                                QStringLiteral("Kde")).kind,
                 QStringLiteral("kde"));
    }

    void anUnknownValueIsLoudNotSilent()
    {
        // "TRADUKU_TRAY=kde" on a GNOME build, or a typo, must not quietly
        // show the other tray: the operator would compare the wrong thing.
        const TrayChoice c = resolveTrayKind(QStringLiteral("kde"),
                                             QStringLiteral("qyt"));
        QCOMPARE(c.kind, QStringLiteral("kde"));          // still usable
        QVERIFY2(!c.warning.isEmpty(),
                 "a typo in TRADUKU_TRAY has to be reported, not ignored");
        QVERIFY(c.warning.contains(QStringLiteral("qyt")));
        QVERIFY(c.warning.contains(QStringLiteral("qt")));   // name the choices
    }

    void namesAreWhatBackendInfoShouldPrint()
    {
        QCOMPARE(trayDisplayName(QStringLiteral("kde")),
                 QStringLiteral("kde (KStatusNotifierItem)"));
        QCOMPARE(trayDisplayName(QStringLiteral("qt")),
                 QStringLiteral("qt (QSystemTrayIcon)"));
        QCOMPARE(trayDisplayName(QStringLiteral("none")),
                 QStringLiteral("none (no tray host)"));
        // Unknown must not print an empty value: --backend-info is read by
        // humans and grepped by e2e.
        QVERIFY(!trayDisplayName(QStringLiteral("wat")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTrayChoice)
#include "tst_traychoice.moc"
