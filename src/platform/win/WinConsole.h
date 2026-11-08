/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

namespace lt {

/*!
 * The console a GUI-subsystem binary does not have.
 *
 * `traduku.exe` is built as a GUI application - the tray app must not put a
 * black window on the desktop - and a GUI binary started from `cmd` has no
 * console of its own: QTextStream(stdout) writes to nothing, so
 * `traduku --translate` would stream a translation nobody can read and
 * `--selftest` would print a report that never arrives. Attaching to the
 * parent's console is the standard fix, and it costs the double-clicked app
 * nothing because Explorer has no console to attach to.
 *
 * The *decision* is `decideConsoleAttach()` in app/AppFlavour.cpp, pure and
 * tested; this is the call.
 */

/// Does this process already have a console we can write to?
bool winHasConsole();

/// Attach to the parent's console and re-point stdout/stderr at it. A no-op
/// when there is no parent console.
void winAttachToParentConsole();


} // namespace lt
