/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinKeyHook.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace lt {

namespace {

std::function<void(const WinKeyTransition &)> g_deliver;
bool g_installed = false;

#ifdef Q_OS_WIN
HHOOK g_hook = nullptr;

QString win32Error(quint32 code)
{
    LPWSTR text = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    QString out;
    if (n && text) {
        out = QString::fromUtf16(reinterpret_cast<const ushort *>(text),
                                 static_cast<int>(n)).trimmed();
    }
    if (text) {
        LocalFree(text);
    }
    return QStringLiteral("%1 (0x%2)")
        .arg(out.isEmpty() ? QStringLiteral("Win32 error") : out)
        .arg(code, 0, 16, QLatin1Char('0'));
}

/// The callback runs with the whole system waiting on this thread: it may
/// convert and hand off, nothing else. Anything slower than that is killed by
/// LowLevelHooksTimeout, and the way a killed hook fails is silent - the app
/// keeps running and simply stops hearing the keyboard.
LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_deliver) {
        const auto *k = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        WinKeyTransition t;
        t.vkCode = k->vkCode;
        t.pressed = (k->flags & LLKHF_UP) == 0;
        // Injected means "somebody's SendInput", including our own tests. It is
        // still a key event as far as the gesture is concerned - what it means
        // is that --backend-info must be able to tell the two apart.
        t.injected = (k->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
        if (wParam == WM_KEYDOWN || wParam == WM_KEYUP || wParam == WM_SYSKEYDOWN
            || wParam == WM_SYSKEYUP) {
            g_deliver(t);
        }
    }
    // Always continue the chain. Returning non-zero here would cancel the key,
    // and the promise this product is built on is that Ctrl+C keeps being
    // Ctrl+C in every other program - tests/integration/tst_winhook_real.cpp
    // measures it with a second, independent hook.
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
#endif

} // namespace

bool winKeyHookInstalled()
{
    return g_installed;
}

WinHotkeyBackend::HookInstaller winKeyHookInstaller()
{
    return [](const std::function<void(const WinKeyTransition &)> &deliver,
              QString *error) {
#ifdef Q_OS_WIN
        if (g_installed) {
            if (error) {
                *error = QStringLiteral("a keyboard observer is already "
                                        "installed in this process");
            }
            return false;
        }
        g_deliver = deliver;
        HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, nullptr, 0);
        if (!hook) {
            g_deliver = nullptr;
            if (error) {
                *error = QStringLiteral("SetWindowsHookExW(WH_KEYBOARD_LL) failed: %1")
                             .arg(win32Error(static_cast<quint32>(GetLastError())));
            }
            return false;
        }
        g_hook = hook;
        g_installed = true;
        return true;
#else
        Q_UNUSED(deliver)
        if (error) {
            *error = QStringLiteral("there is no keyboard hook on this platform");
        }
        return false;
#endif
    };
}

WinHotkeyBackend::HookUninstaller winKeyHookUninstaller()
{
    return []() {
#ifdef Q_OS_WIN
        if (g_hook) {
            UnhookWindowsHookEx(g_hook);
            g_hook = nullptr;
        }
#endif
        g_installed = false;
        g_deliver = nullptr;
    };
}

} // namespace lt
