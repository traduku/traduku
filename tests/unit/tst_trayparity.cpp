/*
    Stage 6b contract: the KDE tray and the Qt tray must be interchangeable.

    The plan (GNOME_PLAN.md D3, solution 4) is to run the GNOME tray code in
    KDE behind LT_TRAY=qt, compare, and only then delete KdeTray. Deleting a
    tray because "it looked fine in a screenshot" is how you ship a missing
    menu item, so this test pins the two together: same interface, same menu,
    same intents. If someone edits one menu and not the other, this fails.
*/
#include <QtTest>
#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QDBusConnection>
#include <QTemporaryDir>

#include "core/settings.h"
#include "interfaces/iautostart.h"
#include "interfaces/itray.h"
#include "platform/kde/KdeTray.h"
#include "platform/qt/QtTray.h"

using namespace lt;

namespace {

class FakeAutostart : public IAutostart
{
public:
    bool isEnabled() const override { return m_on; }
    bool setEnabled(bool on) override
    {
        m_on = on;
        return true;
    }
    QString backendName() const override { return QStringLiteral("fake"); }
    bool m_on = false;
};

/// Enabled, non-separator entries - what the operator can actually do.
///
/// Disabled rows are excluded on purpose: QtTray carries a "double-click or
/// middle-click to translate" hint because Ubuntu's appindicator host is
/// measured to ignore the SNI ToolTip property, while in Plasma the tooltip
/// works and a single click is Activate, so repeating the hint there would be
/// wrong. That is the one allowed difference between the two trays; every
/// actionable item must still match.
QStringList actionableMenuTexts(ITray *tray)
{
    QStringList out;
    for (const QAction *a : tray->menu()->actions()) {
        if (!a->isSeparator() && a->isEnabled()) {
            out << a->text();
        }
    }
    return out;
}

QAction *findByText(ITray *tray, const QString &text)
{
    for (QAction *a : tray->menu()->actions()) {
        if (a->text() == text) {
            return a;
        }
    }
    return nullptr;
}

} // namespace

class TestTrayParity : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString path() const { return m_dir.path() + QStringLiteral("/config.ini"); }

    bool dbusAvailable() const { return QDBusConnection::sessionBus().isConnected(); }

private slots:
    void bothTraysAreTheSameInterface()
    {
        Settings s(path());
        FakeAutostart a1, a2;
        KdeTray kde(&s, &a1);
        QtTray qt(&s, &a2);

        // The shell must be able to hold an ITray* and connect once, no matter
        // which tray the build was configured with.
        ITray *k = qobject_cast<ITray *>(&kde);
        ITray *q = qobject_cast<ITray *>(&qt);
        QVERIFY2(k, "KdeTray must implement ITray");
        QVERIFY2(q, "QtTray must implement ITray");
    }

    void actionableMenusAreIdentical()
    {
        if (!dbusAvailable()) {
            QSKIP("KStatusNotifierItem needs a session bus");
        }
        Settings s(path());
        FakeAutostart a1, a2;
        KdeTray kde(&s, &a1);
        QtTray qt(&s, &a2);

        const QStringList kdeMenu = actionableMenuTexts(&kde);
        const QStringList qtMenu = actionableMenuTexts(&qt);
        QVERIFY2(!kdeMenu.isEmpty(), "the KDE tray produced no menu at all");
        QCOMPARE(qtMenu, kdeMenu);
    }

    void everyEntryExistsInBoth()
    {
        if (!dbusAvailable()) {
            QSKIP("KStatusNotifierItem needs a session bus");
        }
        Settings s(path());
        FakeAutostart a1, a2;
        KdeTray kde(&s, &a1);
        QtTray qt(&s, &a2);

        const QStringList required{
            QStringLiteral("Translate Clipboard Now"),
            QStringLiteral("Translate To"),
            QStringLiteral("Settings…"),
            QStringLiteral("Launch on Login"),
            QStringLiteral("Quit")};
        for (const QString &text : required) {
            QVERIFY2(findByText(&kde, text), qPrintable(QStringLiteral("KDE lacks %1").arg(text)));
            QVERIFY2(findByText(&qt, text), qPrintable(QStringLiteral("Qt lacks %1").arg(text)));
            // checkable where it matters (language list is inside "Translate To")
            if (text == QLatin1String("Launch on Login")) {
                QCOMPARE(findByText(&kde, text)->isCheckable(),
                         findByText(&qt, text)->isCheckable());
            }
        }
    }

    void bothEmitTheSameIntents()
    {
        if (!dbusAvailable()) {
            QSKIP("KStatusNotifierItem needs a session bus");
        }
        Settings s(path());
        FakeAutostart a1, a2;
        KdeTray kde(&s, &a1);
        QtTray qt(&s, &a2);

        // Pointers to ITray's signals, not names: the signals are declared on
        // the interface, and a name lookup on the derived moc object is exactly
        // the kind of thing that silently reports "no such signal".
        struct Case { QString text; void (ITray::*signal)(); };
        const QList<Case> cases{
            {QStringLiteral("Translate Clipboard Now"), &ITray::translateRequested},
            {QStringLiteral("Settings…"), &ITray::settingsRequested},
            {QStringLiteral("Quit"), &ITray::quitRequested}};

        for (const Case &c : cases) {
            QSignalSpy k(&kde, c.signal);
            QSignalSpy q(&qt, c.signal);
            QVERIFY2(k.isValid(), qPrintable(QStringLiteral("KdeTray lacks %1").arg(c.text)));
            QVERIFY2(q.isValid(), qPrintable(QStringLiteral("QtTray lacks %1").arg(c.text)));
            QAction *ka = findByText(&kde, c.text);
            QAction *qa = findByText(&qt, c.text);
            QVERIFY(ka);
            QVERIFY(qa);
            ka->trigger();
            qa->trigger();
            QCOMPARE(k.count(), 1);
            QCOMPARE(q.count(), 1);
        }
    }

    void theStatusRowIsIdenticalAndUnactionable()
    {
        if (!dbusAvailable()) {
            QSKIP("KStatusNotifierItem needs a session bus");
        }
        Settings s(path());
        FakeAutostart a1, a2;
        KdeTray kde(&s, &a1);
        QtTray qt(&s, &a2);

        // "Which build, which tray" has to be readable from the panel. The
        // operator could not tell the two apart after a Start-Menu relaunch
        // that quietly brought back the installed binary.
        const QString label = QStringLiteral("Traduku 0.3.0 · tray qt [TRADUKU_TRAY]");
        kde.setStatusLabel(label);
        qt.setStatusLabel(label);
        for (ITray *t : {static_cast<ITray *>(&kde), static_cast<ITray *>(&qt)}) {
            QAction *row = nullptr;
            for (QAction *a : t->menu()->actions()) {
                if (a->text() == label) {
                    row = a;
                }
            }
            QVERIFY2(row, qPrintable(QStringLiteral("missing status row in %1")
                                         .arg(t->metaObject()->className())));
            QCOMPARE(row->isEnabled(), false);
        }
    }

    void bothSpeakBusyAndToolTip()
    {
        Settings s(path());
        FakeAutostart a1, a2;
        KdeTray kde(&s, &a1);
        QtTray qt(&s, &a2);

        // The AppController drives both through ITray only - if a method lived
        // on one class but not the interface, this would not compile.
        ITray *trays[2] = {&kde, &qt};
        for (ITray *t : trays) {
            t->setBusy(true);
            t->setToolTipSub(QStringLiteral("x"));
            t->show();
            QVERIFY(!t->menu()->isEmpty());
        }
    }
};

QTEST_MAIN(TestTrayParity)
#include "tst_trayparity.moc"
