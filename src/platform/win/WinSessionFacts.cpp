/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinSessionInfo.h"

#include <QGuiApplication>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wtsapi32.h>

/// Present in newer SDK headers, missing from some MinGW winuser.h - and the
/// number is an ABI constant, so spelling it out is a fact, not a guess.
#ifndef SM_SERVICES_SESSION
#define SM_SERVICES_SESSION 0x2000
#endif
#endif

namespace lt {

WinSessionFacts queryWinSessionFacts(const QString &qpaName)
{
    WinSessionFacts f;
    f.qpaName = qpaName.isEmpty() ? QGuiApplication::platformName() : qpaName;
#ifdef Q_OS_WIN
    f.sessionId = WTSGetActiveConsoleSessionId();
    f.remoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;
    HDESK desk = GetThreadDesktop(GetCurrentThreadId());
    f.interactiveDesktop = GetSystemMetrics(SM_SERVICES_SESSION) == 0
                        && desk != nullptr;

    wchar_t desktop[256] = {0};
    if (desk && GetUserObjectInformationW(desk, UOI_NAME, desktop, sizeof(desktop),
                                         nullptr)) {
        f.desktopName = QString::fromUtf16(reinterpret_cast<const ushort *>(desktop));
    }
    // A remote session whose console is someone else's is worth knowing about,
    // but the line is already telling the truth without it: what matters is
    // "remote" and "is there a desktop".
#endif
    return f;
}

} // namespace lt
