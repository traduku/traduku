#pragma once

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
 */
class KdeTray : public QObject
{
    Q_OBJECT
public:
    KdeTray(Settings *settings, IAutostart *autostart, QObject *parent = nullptr);
    ~KdeTray() override;

    void setBusy(bool busy);
    void setToolTipSub(const QString &sub);

Q_SIGNALS:
    void translateRequested();
    void targetLanguagePicked(const QString &code);
    void settingsRequested();

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
};

} // namespace lt
