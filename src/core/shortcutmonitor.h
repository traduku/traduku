#pragma once

#include "core/doublepressdetector.h"
#include "interfaces/ihotkeybackend.h"

#include <QObject>

namespace lt {

class IGlobalShortcut;
class Settings;

/*!
 * Turns a *single-press* global shortcut (KGlobalAccel, GNOME Shortcuts,
 * ...) into the double-Ctrl+C gesture: every triggered() is fed to a core
 * DoublePressDetector as one synthetic Ctrl+C press, so "press the
 * fallback shortcut twice quickly" behaves exactly like the passive
 * monitor. The registered sequence and interval follow the settings live.
 */
class ShortcutMonitor : public IHotkeyBackend
{
    Q_OBJECT
public:
    ShortcutMonitor(IGlobalShortcut *shortcut, Settings *settings,
                    QObject *parent = nullptr);

    bool start() override;
    void stop() override;
    QString backendName() const override { return QStringLiteral("shortcut"); }

    void setClock(DoublePressDetector::Clock clock);   // test seam

private:
    void applyFromSettings();
    void onTriggered();
    // activated() is inherited from IHotkeyBackend and re-emitted by the
    // internal DoublePressDetector.

    IGlobalShortcut *m_shortcut;
    Settings *m_settings;
    DoublePressDetector m_detector;
    bool m_started = false;
};

} // namespace lt
