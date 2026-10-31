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
};

} // namespace lt
