#pragma once

#include "interfaces/itray.h"

#include <QHash>
#include <QObject>
#include <QString>

class KStatusNotifierItem;
class QMenu;
class QAction;

namespace lt {

class IAutostart;
class Settings;

/*!
 * Plasma tray presence (KStatusNotifierItem): menus and clicks only emit
 * intents - all behaviour is the AppController's. This is the only class
 * in the tree that touches KF6::StatusNotifierItem; the GNOME layer will
 * provide the same signals via AppIndicator.
 *
 * Implements ITray so it is interchangeable with QtTray (LT_TRAY /
 * TRADUKU_TRAY). It stays the KDE default, and may only be deleted after
 * tests/unit/tst_trayparity.cpp and the operator agree the Qt tray behaves
 * the same in a live Plasma session (GNOME_PLAN.md Stage 6b).
 */
class KdeTray : public ITray
{
    Q_OBJECT
public:
    KdeTray(Settings *settings, IAutostart *autostart, QObject *parent = nullptr);
    ~KdeTray() override;

    QMenu *menu() const override;
    void setBusy(bool busy) override;
    void setToolTipSub(const QString &sub) override;
    void show() override;
    bool isAvailable() const override;
    QString backendName() const override;
    void setStatusLabel(const QString &text) override;

private:
    void buildMenu();
    void syncLanguageChecks();
    void syncAutostartCheck();

    KStatusNotifierItem *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    Settings *m_settings;
    IAutostart *m_autostart;
    QHash<QString, QAction *> m_languageActions;
    QAction *m_autostartAction = nullptr;
    QAction *m_statusAction = nullptr;
};

} // namespace lt
