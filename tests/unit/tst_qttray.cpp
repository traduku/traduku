/*
    QtTray: QSystemTrayIcon presence for both shells (GNOME_PLAN.md D3).

    Measured, not assumed: Qt's tray on Wayland is Qt's own generic DBus SNI
    client (libQt6Gui: QDBusTrayIcon + StatusNotifierItemAdaptor), present from
    Qt 6.4 through 6.10 in the distro builds we target. It renders and its menu
    works on GNOME 50.1 and in Plasma, so a hand-rolled SNI client is not
    needed - see tools/spike/tray-spike.cpp for the comparison that decided it.

    The behaviour is a copy of KdeTray (the operator's instruction): left click
    translates, right click opens the app menu. KdeTray stays until the Qt one
    is verified in a live KDE session.
*/
#include <QtTest>
#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>

#include "core/settings.h"
#include "interfaces/iautostart.h"
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
        ++m_calls;
        return true;
    }
    QString backendName() const override { return QStringLiteral("fake"); }

    bool m_on = false;
    int m_calls = 0;
};

QStringList menuTexts(const QMenu *menu)
{
    QStringList out;
    for (const QAction *a : menu->actions()) {
        if (!a->isSeparator()) {
            out << a->text();
        }
    }
    return out;
}

} // namespace

class TestQtTray : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString path() const { return m_dir.path() + QStringLiteral("/config.ini"); }

private slots:
    void menuCopiesTheKdeTray()
    {
        Settings s(path());
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);

        // The KDE menu, item by item - the operator is to see the same thing
        // in GNOME, only drawn by Qt instead of KStatusNotifierItem.
        const QStringList expected = {QStringLiteral("Translate Clipboard Now"),
                                      QStringLiteral("Translate To"),
                                      QStringLiteral("Settings…"),
                                      QStringLiteral("Launch on Login"),
                                      QStringLiteral("Quit"),
                                      // the affordance hint, see menuCarriesTheClickHint()
                                      QStringLiteral("Click to translate (double-click if this menu opens instead)")};
        // QCOMPARE is a macro: the braces have to stay out of its arguments.
        QCOMPARE(menuTexts(tray.menu()), expected);

        QMenu *langs = nullptr;
        for (QAction *a : tray.menu()->actions()) {
            if (a->menu()) {
                langs = a->menu();
            }
        }
        QVERIFY(langs);
        QVERIFY(langs->actions().size() > 3);
    }

    void currentLanguageIsChecked()
    {
        Settings s(path());
        s.setTargetLanguage(QStringLiteral("en"));
        s.save();
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);

        int checked = 0;
        bool englishChecked = false;
        for (QAction *a : tray.menu()->actions()) {
            if (!a->menu()) {
                continue;
            }
            for (QAction *lang : a->menu()->actions()) {
                if (lang->isChecked()) {
                    ++checked;
                    if (lang->data().toString() == QLatin1String("en")) {
                        englishChecked = true;
                    }
                }
            }
        }
        QCOMPARE(checked, 1);
        QVERIFY2(englishChecked, "the checked entry must be the configured language");
    }

    void pickingALanguageWritesSettings()
    {
        Settings s(path());
        s.setTargetLanguage(QStringLiteral("ru"));
        s.save();
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);

        QAction *toEnglish = nullptr;
        for (QAction *a : tray.menu()->actions()) {
            if (!a->menu()) {
                continue;
            }
            for (QAction *lang : a->menu()->actions()) {
                if (lang->data().toString() == QLatin1String("en")) {
                    toEnglish = lang;
                }
            }
        }
        QVERIFY(toEnglish);
        QSignalSpy picked(&tray, &QtTray::targetLanguagePicked);
        toEnglish->trigger();
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.first().first().toString(), QStringLiteral("en"));
        QCOMPARE(Settings(path()).targetLanguage(), QStringLiteral("en"));
    }

    void autostartEntryTogglesTheBackendAndStaysChecked()
    {
        Settings s(path());
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);

        QAction *entry = nullptr;   // find by text, not by position
        for (QAction *a : tray.menu()->actions()) {
            if (a->text() == QLatin1String("Launch on Login")) {
                entry = a;
            }
        }
        QVERIFY(entry);
        QVERIFY(entry->isCheckable());
        QCOMPARE(entry->isChecked(), false);

        entry->trigger();
        QCOMPARE(autostart.m_on, true);
        QCOMPARE(entry->isChecked(), true);
    }

    void translateEntryAndLeftClickBothRequestTranslation()
    {
        Settings s(path());
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);
        QSignalSpy translate(&tray, &QtTray::translateRequested);

        for (QAction *a : tray.menu()->actions()) {
            if (a->text() == QLatin1String("Translate Clipboard Now")) {
                a->trigger();
            }
        }
        QCOMPARE(translate.count(), 1);

        // KdeTray maps activateRequested (left click) to translate; a
        // QSystemTrayIcon gives a Trigger first and a DoubleClick after, so
        // the double must not translate twice.
        tray.activationRequested(QSystemTrayIcon::Trigger);
        QCOMPARE(translate.count(), 2);
        tray.activationRequested(QSystemTrayIcon::DoubleClick);
        QCOMPARE(translate.count(), 2);
        tray.activationRequested(QSystemTrayIcon::MiddleClick);
        QCOMPARE(translate.count(), 3);
        // Right click belongs to the menu, not to us.
        tray.activationRequested(QSystemTrayIcon::Context);
        QCOMPARE(translate.count(), 3);
    }

    void quitClosesTheApplication()
    {
        Settings s(path());
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);
        QSignalSpy quit(&tray, &QtTray::quitRequested);
        for (QAction *a : tray.menu()->actions()) {
            if (a->text() == QLatin1String("Quit")) {
                a->trigger();
            }
        }
        // The tray asks; the shell quits. Asserting aboutToQuit here would
        // need a running event loop and would test Qt, not us.
        QCOMPARE(quit.count(), 1);
    }

    void tooltipCarriesTheHintAndTheBusyState()
    {
        Settings s(path());
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);
        QVERIFY(tray.icon()->toolTip().contains(QStringLiteral("Ctrl+C")));
        // Measured on Ubuntu's appindicator host (indicatorStatusIcon.js
        // vfunc_button_press_event): a SINGLE left click always ends in
        // menu.toggle(); Activate is only sent by a DOUBLE click (open() ->
        // ActivateAsync) or the middle button. If the tooltip does not say so,
        // every GNOME user concludes the icon is broken - exactly what
        // happened in review.
        QVERIFY2(tray.icon()->toolTip().contains(QStringLiteral("double-click")),
                 "the affordance the panel actually offers must be discoverable");

        tray.setBusy(true);
        QVERIFY2(tray.icon()->toolTip().contains(QStringLiteral("Translating")),
                 "no overlay icons in QSystemTrayIcon, so busy has to be visible "
                 "in the tooltip");
        tray.setBusy(false);
        QVERIFY(!tray.icon()->toolTip().contains(QStringLiteral("Translating")));
    }

    void menuCarriesTheClickHint()
    {
        // Measured: Ubuntu's appindicator host never reads the SNI ToolTip
        // property (zero uses in appIndicator.js) and hover only un-dims the
        // icon (indicatorStatusIcon.js _onHoverChanged). So a tooltip-only
        // hint is invisible here, and a single left click - what everyone
        // tries first - opens the menu. The menu is where the hint has to be.
        Settings s(path());
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);

        QAction *hint = nullptr;
        for (QAction *a : tray.menu()->actions()) {
            if (a->text().contains(QLatin1String("double-click"), Qt::CaseInsensitive)) {
                hint = a;
            }
        }
        QVERIFY2(hint, "the panel does not show tooltips; the click hint must be in the menu");
        QCOMPARE(hint->isEnabled(), false);   // an instruction, not an action
    }

    void itAlwaysHasAnIconEvenWithoutATheme()
    {
        // Measured: our process does not resolve the desktop's icon theme
        // (QIcon::hasThemeIcon() was false on this GNOME box), and Traduku is
        // not installed, so no theme knows its name. A null icon means the
        // host paints the "..." placeholder, which is what the Qt-tray spike
        // looked like - so a real pixmap is a requirement, not cosmetics.
        Settings s(path());
        FakeAutostart autostart;
        QtTray tray(&s, &autostart);
        QVERIFY2(!tray.icon()->icon().isNull(), "the tray would render nothing");
    }
};

QTEST_MAIN(TestQtTray)
#include "tst_qttray.moc"
