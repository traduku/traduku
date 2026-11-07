#pragma once

#include "interfaces/iautostart.h"

#include <QString>
#include <QStringList>

namespace lt {

/*!
 * Login autostart through the XDG autostart spec: an entry named
 * `<id>.desktop` in $XDG_CONFIG_HOME/autostart, honoured by KDE, GNOME,
 * XFCE, ... with no DE libraries.
 *
 * The interesting half is not writing our own file, it is agreeing with an
 * entry we do not own. traduku-gnome INSTALLS /etc/xdg/autostart/traduku.desktop
 * (a GNOME shell that asked instead would ship an app that does nothing until
 * someone opens a terminal), so on that desktop the app starts at login
 * whatever the tray menu ticked - and the tick only ever looked at the user
 * file, which meant it read "off" while the app autostarted, and un-ticking
 * deleted a file that was not why it started.
 *
 * The spec's answer is a same-named user file that shadows the system one, so
 * that is what disabling writes. `systemDirs` is injectable because /etc is not
 * a place a unit test may write.
 */
class XdgAutostart : public IAutostart
{
public:
    XdgAutostart(const QString &desktopId, const QString &execPath,
                 const QStringList &systemDirs = {});

    bool isEnabled() const override;
    bool setEnabled(bool on) override;
    QString backendName() const override { return QStringLiteral("xdg"); }

    /// Where the user's own entry lives (created only when they choose).
    QString filePath() const;
    /// True when a package (or any system dir) installed the entry.
    bool isSystemInstalled() const;
    /// What --backend-info and the menu report: is the app coming back.
    QString diagnostic() const;

private:
    QStringList m_systemDirs;
    QString m_desktopId;
    QString m_execPath;
};

} // namespace lt
