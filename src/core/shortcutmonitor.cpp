#include "shortcutmonitor.h"

#include "core/settings.h"
#include "interfaces/iglobalshortcut.h"

namespace lt {

ShortcutMonitor::ShortcutMonitor(IGlobalShortcut *shortcut, Settings *settings,
                                 QObject *parent)
    : IHotkeyBackend(parent)
    , m_shortcut(shortcut)
    , m_settings(settings)
{
    connect(&m_detector, &DoublePressDetector::activated, this,
            [this]() { Q_EMIT activated(); });
    connect(m_settings, &Settings::changed, this, &ShortcutMonitor::applyFromSettings);
}

bool ShortcutMonitor::start()
{
    connect(m_shortcut, &IGlobalShortcut::triggered,
            this, &ShortcutMonitor::onTriggered);
    m_started = true;
    applyFromSettings();
    return m_shortcut->registerShortcut(m_settings->globalShortcut());
}

void ShortcutMonitor::stop()
{
    if (m_started) {
        disconnect(m_shortcut, nullptr, this, nullptr);
        m_shortcut->unregister();
        m_started = false;
    }
}

void ShortcutMonitor::setClock(DoublePressDetector::Clock clock)
{
    m_detector.setClock(std::move(clock));
}

void ShortcutMonitor::applyFromSettings()
{
    m_detector.setIntervalMs(qint64(m_settings->pressInterval() * 1000.0));
    if (m_started) {
        m_shortcut->registerShortcut(m_settings->globalShortcut());
    }
}

void ShortcutMonitor::onTriggered()
{
    // Feed one complete synthetic Ctrl+C chord to the detector.
    m_detector.keyEvent(DoublePressDetector::Press, KeyCtrlL, 0);
    m_detector.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
    m_detector.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
    m_detector.keyEvent(DoublePressDetector::Release, KeyCtrlL, 0);
}

} // namespace lt
