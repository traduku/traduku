/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include "interfaces/ihotkeybackend.h"
#include "WinGesturePolicy.h"

#include <functional>
#include <memory>

namespace lt {

class DoublePressDetector;

/*!
 * The Windows gesture: watch the keyboard, do not grab it.
 *
 * `WH_KEYBOARD_LL` is the only channel that sees the literal Ctrl+C+C without
 * taking it away from the application being typed into - `RegisterHotKey`
 * would *own* the combination, and "press Ctrl+C twice" is a combination every
 * copy in the world already uses (ROADMAP.md §7.2). It is the Windows twin of
 * reading /dev/input instead of EVIOCGRAB, for the same reason.
 *
 * The installer is injected. That is not decoration: it is what lets
 * tests/unit/tst_wingesture.cpp assert "a hook that failed says why", and it
 * is what keeps this file free of windows.h so the class compiles, links and
 * runs its unit suite on every platform - the OS-specific part is one
 * function in WinKeyHook.cpp.
 *
 * The hook callback runs while the system is blocked waiting for us, so it
 * must not translate: transitions are handed to the event loop and the policy
 * runs there. A gesture that froze the desktop for the duration of an HTTP
 * round-trip would be a bug report titled "Windows is slow".
 */
class WinHotkeyBackend : public IHotkeyBackend
{
public:
    /// Installs the OS observer; on success `deliver` becomes the sink the OS
    /// calls. false = this machine cannot give us a keyboard (session 0, a
    /// disconnected RDP session), and `error` must say which.
    using HookInstaller =
        std::function<bool(const std::function<void(const WinKeyTransition &)> &deliver,
                           QString *error)>;
    using HookUninstaller = std::function<void()>;

    WinHotkeyBackend(HookInstaller installer, HookUninstaller uninstaller = {},
                     QObject *parent = nullptr);
    ~WinHotkeyBackend() override;

    bool start() override;
    void stop() override;
    /// The name --backend-info and the `ready` line print. "win-ll-hook"
    /// because a listener that is not named is a listener that gets confused
    /// with another one (see modeName() on Linux).
    QString backendName() const override { return QStringLiteral("win-ll-hook"); }

    void setIntervalMs(qint64 ms);
    qint64 intervalMs() const;

    QString lastError() const { return m_error; }

    /// Real fingers vs SendInput, for the report and for the tests.
    quint64 realEvents() const;
    quint64 injectedEvents() const;

    /// Public because the injected hook calls it from the thread that owns the
    /// hook, and a queued copy of it is what the event loop actually runs.
    void onTransition(const WinKeyTransition &t);

private:
    std::unique_ptr<DoublePressDetector> m_detector;
    WinGesturePolicy m_policy;
    HookInstaller m_installer;
    HookUninstaller m_uninstaller;
    QString m_error;
    bool m_running = false;
};

} // namespace lt
