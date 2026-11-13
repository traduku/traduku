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

    /// Whether the app comes back at login, *and who decided*: the user's own
    /// entry, an entry somebody else wrote, or one the platform installed.
    ///
    /// It is on the interface because two commands must give the same answer -
    /// the tray menu's tick and `traduku --autostart=status` - and because
    /// "off" printed next to an app that keeps autostarting is the exact bug
    /// tst_xdgautostart was written for. The default is empty so an
    /// implementation stays usable while it is thin; every real one fills it.
    virtual QString diagnostic() const { return {}; }
};

} // namespace lt
