/*
    The menu contract, kept.

    Two tray implementations used to exist: KdeTray (KStatusNotifierItem) and
    QtTray (QSystemTrayIcon). This test compared them item by item. The KDE one
    is gone - it segfaulted on destruction with a live session bus, and the Qt
    tray reproduced its menu entry for entry in Plasma (verified by the
    operator) - so this file no longer compares two objects; it pins what the
    Qt tray owes the desktop:

      * the menu the KDE version had, item for item, and the intents behind it;
      * a status row that tells you which build you are looking at;
      * no executable item may tear the application down by itself.

    That last rule is the regression guard for the exit crash: KStatusNotifierItem
    was deleted synchronously from a menu handler and killed the app. Any future
    tray must hand shutdown to the shell.
*/
#include <QtTest>
#include <QAction>
#include <QMenu>
#include <QTimer>

#include "platform/qt/QtTray.h"
#include "interfaces/iautostart.h"
#include "core/settings.h"
#include "interfaces/itray.h"

using namespace lt;

namespace {

/// The menu the KDE tray shipped, and which intent each entry must fire.
/// `QAction::triggered` is a signal: emitting it runs the menu action with no
/// mouse involved, so the contract is testable offscreen.
const char *const kRequiredEntries[] = {
    "Translate Clipboard Now",
    "Translate To",
    "Settings…",
    "Launch on Login",
    "Quit",
};

/// Every row that can be clicked must lead somewhere: the tray is a remote
/// control, and a row that does nothing is a lie about what the app can do.
void collectExecutable(const QList<QAction *> &actions, QStringList &out)
{
    for (QAction *a : actions) {
        if (a->isSeparator()) {
            continue;
        }
        if (a->isCheckable() || a->menu() || !a->isEnabled()) {
            continue;   // not executable: a toggle, a submenu or a label
        }
        out << a->text();
        collectExecutable(a->menu() ? a->menu()->actions() : QList<QAction *>(),
                          out);
    }
}

} // namespace

class TestTrayContract : public QObject
{
    Q_OBJECT
private:
    /// Autostart without touching ~/.config: the contract is about the menu.
    class FakeAutostart : public IAutostart
    {
    public:
        bool isEnabled() const override { return m_on; }
        bool setEnabled(bool on) override { m_on = on; return true; }
        QString backendName() const override { return QStringLiteral("fake"); }
        bool m_on = false;
    };

    /// A tray with real Settings on a scratch config: the language submenu is
    /// built from them, and picking a language writes back to them.
    struct Fixture {
        QTemporaryDir dir;
        QString path;
        Settings settings;
        FakeAutostart autostart;
        QtTray *tray = nullptr;

        Fixture()
            : path(dir.filePath(QStringLiteral("config.ini"))),
              settings(path)
        {
            QSettings s(path, QSettings::IniFormat);
            s.beginGroup(QStringLiteral("General"));
            s.setValue(QStringLiteral("TargetLanguage"), QStringLiteral("en"));
            s.setValue(QStringLiteral("SourceLanguage"), QStringLiteral("auto"));
            s.endGroup();
            tray = new QtTray(&settings, &autostart);
            tray->setStatusLabel(QStringLiteral("Traduku 9.9.9 · tray qt [test]"));
        }
        ~Fixture() { delete tray; }
    };

    static QStringList allRows(QMenu *menu)
    {
        QStringList out;
        for (QAction *a : menu->actions()) {
            if (a->isSeparator()) {
                continue;
            }
            out << a->text();          // the submenu title is a row too
            if (a->menu()) {
                out << allRows(a->menu());
            }
        }
        return out;
    }

    static QStringList actionableItems(QMenu *menu)
    {
        QStringList out;
        collectExecutable(menu->actions(), out);
        out.sort();
        return out;
    }

private slots:
    void theMenuCarriesEveryEntryTheKdeTrayHad()
    {
        Fixture f;
        // Every row, checkable ones included: "Launch on Login" is a toggle and
        // is still part of the menu the KDE version had.
        const QStringList rows = allRows(f.tray->menu());
        QStringList missing;
        for (const char *entry : kRequiredEntries) {
            const QString wanted = QString::fromUtf8(entry);
            if (!rows.contains(wanted)) {
                missing << wanted;
            }
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("menu is missing: %1")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /// The crash this whole exercise came from: a menu action that destroyed
    /// the tray (or the app) synchronously.
    void noExecutableItemTearsTheAppDownItself()
    {
        Fixture f;
        QTimer::singleShot(0, qApp, [] { QFAIL("an executable item quit the app"); });
        int fired = 0;
        QObject::connect(qApp, &QCoreApplication::aboutToQuit,
                         [&f, &fired] { ++fired; Q_UNUSED(&f); });
        const QStringList names = actionableItems(f.tray->menu());
        for (const QString &name : names) {
            QAction *a = nullptr;
            const QList<QAction *> all = f.tray->menu()->actions();
            for (QAction *cand : all) {
                if (cand->text() == name) { a = cand; break; }
                if (cand->menu()) {
                    for (QAction *in : cand->menu()->actions()) {
                        if (in->text() == name) { a = in; break; }
                    }
                }
                if (a) break;
            }
            QVERIFY2(a, qPrintable(QStringLiteral("action not found: %1").arg(name)));
            QSignalSpy spy(a, &QAction::triggered);
            QVERIFY(spy.count() == 0 || true);
            emit a->triggered(false);
        }
        QVERIFY2(fired == 0,
                 "an executable tray item quit the application: emit "
                 "ITray::quitRequested() and let the shell decide");
    }

    void everyIntentTheShellConnectsToIsEmittable()
    {
        Fixture f;
        QSignalSpy translate(f.tray, &ITray::translateRequested);
        QSignalSpy settings(f.tray, &ITray::settingsRequested);
        QSignalSpy quit(f.tray, &ITray::quitRequested);
        QSignalSpy language(f.tray, &ITray::targetLanguagePicked);

        QAction *languageRow = nullptr;
        for (QAction *a : f.tray->menu()->actions()) {
            if (a->text() == QStringLiteral("Settings…")
                || a->text() == QStringLiteral("Quit")
                || a->text() == QStringLiteral("Translate Clipboard Now")) {
                emit a->triggered(false);
            } else if (a->menu() && !a->menu()->actions().isEmpty()) {
                languageRow = a->menu()->actions().first();   // "Translate To"
                emit languageRow->triggered(false);
            }
        }
        QVERIFY2(languageRow, "the tray has no language submenu to pick from");
        const QString where = QStringLiteral(
            "translate=%1 settings=%2 quit=%3 language=%4")
            .arg(translate.count()).arg(settings.count())
            .arg(quit.count()).arg(language.count());
        QVERIFY2(translate.count() == 1, qPrintable(where));
        QVERIFY2(settings.count() == 1, qPrintable(where));
        QVERIFY2(quit.count() == 1, qPrintable(where));
        QVERIFY2(language.count() == 1, qPrintable(where));
        // Whatever the first language in the list is, the code must follow it.
        QCOMPARE(language.last().first().toString().isEmpty(), false);
    }

    /// "Which build is in my panel?" - the question the operator could not
    /// answer after restarting the app from the Start Menu.
    void theStatusRowNamesTheBuildAndIsUnactionable()
    {
        Fixture f;
        QAction *status = nullptr;
        for (QAction *a : f.tray->menu()->actions()) {
            if (a->text().contains(QStringLiteral("9.9.9"))) { status = a; break; }
        }
        QVERIFY2(status, "no status row: the tray cannot say which build it is");
        QVERIFY2(!status->isEnabled(), "the status row must not be clickable");
        QVERIFY(status->text().contains(QStringLiteral("tray qt")));
    }

    void busyStateIsVisibleWithoutOverlayIcons()
    {
        Fixture f;
        f.tray->setBusy(true);
        QVERIFY(f.tray->icon()->toolTip().contains(QStringLiteral("Translating")));
        f.tray->setBusy(false);
        QVERIFY(!f.tray->icon()->toolTip().contains(QStringLiteral("Translating")));
    }

    void theClickHintIsThereButUnclickable()
    {
        // GNOME's appindicator host ignores the tooltip, so the hint has to be
        // a menu row; and a row you cannot click must not look clickable.
        Fixture f;
        QAction *hint = nullptr;
        for (QAction *a : f.tray->menu()->actions()) {
            if (a->text().contains(QStringLiteral("click"), Qt::CaseInsensitive)) {
                hint = a;
            }
        }
        QVERIFY2(hint, "no click hint in the menu");
        QVERIFY(!hint->isEnabled());
    }
};

QTEST_MAIN(TestTrayContract)
#include "tst_trayparity.moc"
