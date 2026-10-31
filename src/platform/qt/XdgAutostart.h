#pragma once

#include "interfaces/iautostart.h"

#include <QString>

namespace lt {

/// Login autostart through the XDG autostart spec
/// ($XDG_CONFIG_HOME/autostart/<id>.desktop) - honoured by KDE, GNOME,
/// XFCE, ... Works without any DE libraries.
class XdgAutostart : public IAutostart
{
public:
    XdgAutostart(const QString &desktopId, const QString &execPath);

    bool isEnabled() const override;
    bool setEnabled(bool on) override;
    QString backendName() const override { return QStringLiteral("xdg"); }

private:
    QString filePath() const;
    QString m_desktopId;
    QString m_execPath;
};

} // namespace lt
