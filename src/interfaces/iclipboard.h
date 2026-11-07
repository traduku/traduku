#pragma once

#include <QString>

namespace lt {

/// Clipboard access, abstracted so each platform layer can supply its own
/// (Qt on X11, wl-clipboard tools on Wayland, pastes-portal later, ...).
class IClipboard
{
public:
    virtual ~IClipboard() = default;
    virtual QString text() const = 0;
    virtual bool setText(const QString &text) = 0;
    virtual QString backendName() const = 0;

    /// Actionable hint when this backend cannot actually see the DE
    /// clipboard - empty means "nothing to complain about". Measured on
    /// GNOME 50 (GNOME_PLAN.md §0b): a background wayland client gets no
    /// data offer at all, so the Qt fallback there is blind; without a
    /// hint the user is told "clipboard is empty" while the real cause is
    /// a missing wl-clipboard package. The app shows this verbatim.
    virtual QString diagnostic() const { return {}; }
};

} // namespace lt
