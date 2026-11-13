/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include "WinHotkeyBackend.h"

namespace lt {

/*!
 * The real `WH_KEYBOARD_LL` observer, and the only place in the Windows layer
 * that includes user32.
 *
 * Kept separate from WinHotkeyBackend on purpose: with the mechanism here, the
 * decision (WinGesturePolicy), the queueing (WinHotkeyBackend) and their tests
 * all exist on machines without a Windows SDK, which is what lets CI on Linux
 * see a regression in the gesture logic instead of only on a Windows host.
 *
 * One hook per process: two observers on one thread would mean two gestures
 * per key. The second start() returns the first hook's result instead of
 * installing a second one.
 */
WinHotkeyBackend::HookInstaller winKeyHookInstaller();
WinHotkeyBackend::HookUninstaller winKeyHookUninstaller();

/// True when a Traduku hook is currently installed. For --backend-info, which
/// must be able to say "installed" as a measured fact rather than as an
/// intention beside the code that installs it.
bool winKeyHookInstalled();

} // namespace lt
