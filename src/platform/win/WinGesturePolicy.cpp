/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinGesturePolicy.h"

#include "core/doublepressdetector.h"

namespace lt {

namespace {

/// VK -> the core detector's key id. The mapping is the entire translation
/// between "a Windows driver reported something" and "the shared state machine
/// saw a key", and it is a table rather than arithmetic because the two
/// namespaces only coincide by luck: VK_C happens to equal KeyC, VK_CONTROL
/// does not equal KeyCtrlL, and the right Ctrl is a third number again.
///
/// The native code is deliberately 0: the detector's native-code set holds the
/// X11 keycode of the physical C (54), and feeding a Windows VK into it would
/// make VK 54 - the digit 6 - part of the gesture on Windows. Physical-key
/// matching here is the VK numbering itself.
bool mapKey(quint32 vk, quint32 *coreId)
{
    switch (vk) {
    case WinVkC:         *coreId = KeyC;      return true;
    case WinVkControl:
    case WinVkLControl:  *coreId = KeyCtrlL;  return true;
    case WinVkRControl:  *coreId = KeyCtrlR;  return true;
    default:             return false;
    }
}

} // namespace

WinGesturePolicy::WinGesturePolicy(DoublePressDetector *detector)
    : m_detector(detector)
{
    QObject::connect(m_detector, &DoublePressDetector::activated, m_detector,
            [this]() { m_fired = true; }, Qt::DirectConnection);
}

bool WinGesturePolicy::feed(const WinKeyTransition &t)
{
    if (t.injected) {
        ++m_injected;
    } else {
        ++m_real;
    }

    quint32 coreId = 0;
    if (!mapKey(t.vkCode, &coreId)) {
        return false;   // every other key on the keyboard is not our business
    }
    const DoublePressDetector::Type type =
        t.pressed ? DoublePressDetector::Press : DoublePressDetector::Release;

    // "Did that press complete a gesture?" is a question only the detector can
    // answer, and it answers it by emitting activated(). The connection is made
    // once, in the ctor, and this latches the emission for the duration of the
    // single keyEvent() call - so a release can never report a gesture, and the
    // backend never has to guess from key codes.
    m_fired = false;
    m_detector->keyEvent(type, coreId, 0);
    return t.pressed && m_fired;
}

void WinGesturePolicy::setIntervalMs(qint64 ms)
{
    m_detector->setIntervalMs(ms);
}

qint64 WinGesturePolicy::intervalMs() const
{
    return m_detector->intervalMs();
}

void WinGesturePolicy::resetGesture()
{
    m_detector->reset();
    m_fired = false;
}

void WinGesturePolicy::resetCounters()
{
    m_real = 0;
    m_injected = 0;
    resetGesture();
}

} // namespace lt
