/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include <QtGlobal>
#include <functional>

namespace lt {

class DoublePressDetector;

/*!
 * One key transition as the low-level keyboard hook reports it.
 *
 * Deliberately a plain struct and deliberately NOT KBDLLHOOKSTRUCT: the
 * gesture decision is then testable without user32, and a test can describe a
 * keyboard instead of interrogating the one it happens to be running on - the
 * rule `ToolFinder` (SoundPlayer) and `DeviceFinder` (evdev) already encode.
 * The hook is the only thing that ever converts the real structure.
 */
struct WinKeyTransition
{
    quint32 vkCode = 0;    ///< Win32 virtual-key code (VK_CONTROL, 'C', ...)
    bool pressed = true;   ///< false = key release
    bool injected = false; ///< LLKHF_INJECTED: synthetic, not a finger
};

/// Virtual-key codes of the gesture, spelled out because including windows.h
/// in a portable header would end the tests that do not have it. The numbers
/// are the Win32 constants and are asserted against the OS in
/// tests/integration/tst_winhook_real.cpp.
constexpr quint32 WinVkControl = 0x11;
constexpr quint32 WinVkLControl = 0xA2;
constexpr quint32 WinVkRControl = 0xA3;
constexpr quint32 WinVkC = 0x43;

/*!
 * VK codes in, gesture out - the whole decision, with no OS in sight.
 *
 * Matching is by virtual-key code, which is what makes it layout-independent:
 * VK codes follow the physical key position of the baseline layout, so a
 * Russian or Greek layout pressing the same key still reports VK_C. That is
 * the Windows twin of "match the physical C by keycode" on X11/evdev, and it
 * is why the gesture never becomes "Ctrl+В" on a Cyrillic desktop.
 *
 * Both spellings of Ctrl are accepted (VK_CONTROL plus the left/right
 * variants), because drivers disagree about which one a hook receives.
 */
class WinGesturePolicy
{
public:
    explicit WinGesturePolicy(DoublePressDetector *detector);

    /// Feed one transition; true when it completed a gesture.
    bool feed(const WinKeyTransition &t);

    void setIntervalMs(qint64 ms);
    qint64 intervalMs() const;

    /// Real fingers and SendInput are not the same fact. --backend-info
    /// prints both counts: "the gesture works" said after only injected keys
    /// would be a test's achievement, not the user's.
    /// Forget the burst without forgetting the counters: stopping must make the
    /// next Ctrl+C the first one, while the counts stay printable afterwards.
    void resetGesture();

    quint64 realCount() const { return m_real; }
    quint64 injectedCount() const { return m_injected; }
    void resetCounters();

private:
    DoublePressDetector *m_detector;
    quint64 m_real = 0;
    quint64 m_injected = 0;
    /// Latched by the detector's activated() during the current keyEvent().
    bool m_fired = false;
};

} // namespace lt
