/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinConsole.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace lt {

bool winHasConsole()
{
#ifdef Q_OS_WIN
    if (GetConsoleWindow() != nullptr) {
        return true;
    }
    // A GUI binary started from a console has no console handle but its
    // standard handles may already be valid pipes - that is `traduku | more`,
    // and attaching anyway would take the output away from the pipe.
    return _isatty(_fileno(stdout)) || _isatty(_fileno(stderr));
#else
    return true;    // every Unix process was started with the terminal it has
#endif
}

void winAttachToParentConsole()
{
#ifdef Q_OS_WIN
    if (winHasConsole()) {
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;       // started from Explorer: there is nothing to attach to
    }
    // The CRT still has the (absent) handles it inherited at startup; re-open
    // the console ones, and keep stdout unbuffered enough that a streamed
    // translation arrives as it is produced rather than at exit.
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);
    _setmode(_fileno(stdout), _O_TEXT);
#endif
}

} // namespace lt
