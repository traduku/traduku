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
    // "Is there somewhere for the output to go?" - not "is it a terminal".
    //
    // The first version asked _isatty(), which on this CRT answers with a
    // device-type *value* (measured: 64 for a console, 0 for anything else),
    // so a redirected run - `traduku --selftest > report.txt`, `--translate |
    // more`, the harness capturing the stream to grep it - looked like a
    // process with nowhere to write. winAttachToParentConsole() then reopened
    // stdout on CONOUT$, which is a screen buffer rather than the pipe the
    // user asked for, and every later line went to a desktop nobody was
    // looking at. The suite now pins this (tst_consolefatal::
    // redirectedStdoutStillGetsTheReport), because from a terminal the bug is
    // invisible: there the process already has a console and nothing attaches.
    //
    // So: any usable standard handle means the launcher decided where the
    // output goes, and a program pointed at a pipe must not steal it
    // back for a console.
    for (const DWORD which : {static_cast<DWORD>(STD_OUTPUT_HANDLE),
                              static_cast<DWORD>(STD_ERROR_HANDLE)}) {
        const HANDLE h = GetStdHandle(which);
        if (h == nullptr || h == INVALID_HANDLE_VALUE) {
            continue;
        }
        switch (GetFileType(h)) {
        case FILE_TYPE_DISK:
        case FILE_TYPE_PIPE:
        case FILE_TYPE_CHAR:
            return true;
        default:
            break;      // unknown: not a reason to hijack the other handle
        }
    }
    return false;
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


QString winModuleFilePath()
{
#ifdef Q_OS_WIN
    // The long-path limit is the documented maximum; a build directory deeper
    // than that is not this product's deployment, and an empty answer is
    // handled by the caller rather than assumed away.
    wchar_t buffer[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, 32767);
    if (n == 0 || n >= 32767) {
        return {};
    }
    return QString::fromWCharArray(buffer, static_cast<qsizetype>(n));
#else
    return {};    // no PE image to ask; the caller falls back to Qt's own tree
#endif
}

} // namespace lt
