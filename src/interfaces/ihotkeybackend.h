#pragma once

#include <QObject>

namespace lt {

/*!
 * Source of the "translate now" gesture. Implementations: passive X11/XI2
 * keyboard monitor (X11+XWayland), InputCapture portal (pure Wayland),
 * a registered global shortcut (fallback), tray clicks, DBus, ...
 * The app layer only ever sees activated().
 */
class IHotkeyBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IHotkeyBackend() override { QObject::disconnect(); }

    /// false = this backend cannot work here (no X display, portal broken).
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual QString backendName() const = 0;

Q_SIGNALS:
    void activated();
};

} // namespace lt
