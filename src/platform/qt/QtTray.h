#pragma once

#include "interfaces/itray.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

class QMenu;
class QAction;

namespace lt {

class IAutostart;
class Settings;

/*!
 * Tray presence for both shells, built on QSystemTrayIcon.
 *
 * Why not our own StatusNotifierItem client: Qt already ships one. On Wayland
 * QSystemTrayIcon falls back to Qt's generic unix tray (QDBusTrayIcon +
 * StatusNotifierItemAdaptor inside libQt6Gui), which this project measured
 * rendering its icon and menu on GNOME 50.1 (Ubuntu's appindicator extension)
 * and which is present in the distro Qt builds from 6.4 to 6.10. The
 * comparison that decided this is tools/spike/tray-spike.cpp, and the SNI
 * client it produced stays there as the documented fallback.
 *
 * Behaviour is a deliberate copy of KdeTray, which stays in the tree until the
 * Qt tray is verified in a live KDE session (GNOME_PLAN.md Stage 6b):
 *   left click (and middle click) -> translateRequested()
 *   right click                   -> the app menu
 * Menu: Translate Clipboard Now | Translate To ▸ | Settings… |
 *       Launch on Login | Quit.
 */
class QtTray : public ITray
{
    Q_OBJECT
public:
    QtTray(Settings *settings, IAutostart *autostart, QObject *parent = nullptr);
    ~QtTray() override;

    /// Without constructing a tray: does this session have a tray host at all?
    /// Used by --backend-info, which must not create an icon to answer.
    /// Is there anything in this session that will draw a StatusNotifier item?
    ///
    /// Not just QSystemTrayIcon::isSystemTrayAvailable(): that answers through
    /// the QPA plugin, so it says "no tray" whenever the platform is offscreen -
    /// and `--backend-info` is built to run headless, where the honest answer
    /// about a Plasma/GNOME session is "yes, the watcher is there". The bus is
    /// asked as well, because the bus is what the tray actually talks to.
    static bool hostSupportsTray();

    QMenu *menu() const override { return m_menu; }
    QString backendName() const override
    {
        return QStringLiteral("qt (QSystemTrayIcon)");
    }
    /// The QSystemTrayIcon itself - for tests and for the tooltip assertions.
    QSystemTrayIcon *icon() const { return m_tray; }

    /// Which build/tray this is, as a disabled row on top of the menu.
    void setStatusLabel(const QString &text) override;

    /// False when the platform has no tray at all. The app must keep working
    /// without one - on GNOME without an SNI extension nothing appears here,
    /// and that is a supported configuration, not an error.
    bool isAvailable() const override;

    void show() override;

    /// QSystemTrayIcon has no overlay icons (KStatusNotifierItem does), so the
    /// busy state is surfaced in the tooltip instead.
    void setBusy(bool busy) override;
    void setToolTipSub(const QString &sub) override;

public slots:
    /// Split out of the signal connection so the click semantics are testable
    /// without a panel: Trigger/MiddleClick translate, DoubleClick must not
    /// translate a second time (Qt sends a Trigger first), ContextMenu belongs
    /// to the menu.
    void activationRequested(QSystemTrayIcon::ActivationReason reason);

    // Signals come from ITray: translateRequested, targetLanguagePicked,
    // settingsRequested, quitRequested.

private:
    void buildMenu();
    void syncLanguageChecks();
    void syncAutostartCheck();
    void refreshToolTip();
    static QIcon createIcon();

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    Settings *m_settings;
    IAutostart *m_autostart;
    QHash<QString, QAction *> m_languageActions;
    QAction *m_autostartAction = nullptr;
    QAction *m_statusAction = nullptr;
    bool m_busy = false;
    QString m_toolTipSub;
};

} // namespace lt
