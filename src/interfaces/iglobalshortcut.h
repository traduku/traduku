#pragma once

#include <QObject>
#include <QString>

namespace lt {

/*!
 * System-wide shortcut registration (KGlobalAccel on Plasma,
 * portal/Shortcuts on GNOME, ...). The registry emits triggered() once per
 * press; pairs of presses within the interval are turned into the
 * translate gesture by a core DoublePressDetector fed by the app layer.
 */
class IGlobalShortcut : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IGlobalShortcut() override { QObject::disconnect(); }

    /// seq: QKeySequence-compatible text, e.g. "Ctrl+Alt+C".
    virtual bool registerShortcut(const QString &seq) = 0;
    virtual void unregister() = 0;
    virtual QString backendName() const = 0;

Q_SIGNALS:
    void triggered();
};

} // namespace lt
