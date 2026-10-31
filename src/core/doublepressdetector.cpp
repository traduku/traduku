#include "doublepressdetector.h"

#include <QDateTime>

namespace lt {

DoublePressDetector::DoublePressDetector(QObject *parent)
    : QObject(parent)
{
    // Defaults mirror the proven prototype: latin c/C, Cyrillic es/ES
    // keysyms and the layout-independent physical keycode 54.
    m_triggerKeys = {KeyC, 0x0063, 0x0441, 0x0421};
    m_triggerNative = {KeyNativeC};
    m_modifierKeys = {KeyCtrlL, KeyCtrlR};
    m_clock = []() { return QDateTime::currentMSecsSinceEpoch(); };
}

void DoublePressDetector::setClock(Clock clock)
{
    m_clock = std::move(clock);
}

void DoublePressDetector::setIntervalMs(qint64 ms)
{
    m_intervalMs = ms;
}

void DoublePressDetector::setModifierProbe(ModifierProbe probe)
{
    m_probe = std::move(probe);
}

void DoublePressDetector::addTriggerKey(quint32 keyId)
{
    m_triggerKeys.insert(keyId);
}

void DoublePressDetector::addTriggerNativeCode(quint32 code)
{
    m_triggerNative.insert(code);
}

void DoublePressDetector::addModifierKey(quint32 keyId)
{
    m_modifierKeys.insert(keyId);
}

bool DoublePressDetector::isTrigger(quint32 keyId, quint32 nativeCode) const
{
    return m_triggerKeys.contains(keyId)
        || (nativeCode != 0 && m_triggerNative.contains(nativeCode));
}

void DoublePressDetector::keyEvent(Type type, quint32 keyId, quint32 nativeCode)
{
    if (m_modifierKeys.contains(keyId)) {
        m_ctrlDown = (type == Press);
        return;
    }

    if (type == Release) {
        if (isTrigger(keyId, nativeCode)) {
            m_cDown = false;
        }
        return;
    }

    // Press
    if (!isTrigger(keyId, nativeCode)) {
        return;
    }
    if (m_cDown) {
        return;                    // keyboard auto-repeat while held
    }
    m_cDown = true;

    const bool ctrlDown = m_ctrlDown || (m_probe && m_probe());
    if (!ctrlDown) {
        return;                    // plain typing of 'c'
    }

    const qint64 now = m_clock();
    if (m_lastPressMs > 0 && (now - m_lastPressMs) > m_intervalMs) {
        m_lastPressMs = 0;         // first press expired, start fresh
    }
    if (m_lastPressMs > 0) {
        m_lastPressMs = 0;
        Q_EMIT activated();
    } else {
        m_lastPressMs = now;
    }
}

void DoublePressDetector::reset()
{
    m_ctrlDown = false;
    m_cDown = false;
    m_lastPressMs = 0;
}

} // namespace lt
