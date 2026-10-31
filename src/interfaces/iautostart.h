#pragma once

#include <QString>

namespace lt {

/// Login-time autostart (KDE: ~/.config/autostart, GNOME: XDG autostart
/// too, systemd user units possible - abstracted behind two methods).
class IAutostart
{
public:
    virtual ~IAutostart() = default;
    virtual bool isEnabled() const = 0;
    virtual bool setEnabled(bool on) = 0;
    virtual QString backendName() const = 0;
};

} // namespace lt
