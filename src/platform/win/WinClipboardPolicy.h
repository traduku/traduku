/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include <QString>

namespace lt {

/*!
 * What TRADUKU_CLIPBOARD must be refused on Windows, and why.
 *
 * There is exactly one clipboard reader here (Qt's, which is a real client of
 * the win32 clipboard), so `TRADUKU_CLIPBOARD=wl` - a habit from a Linux
 * machine where the choice is wl-clipboard / X11 / Qt - has no meaning. The
 * product answer is a sentence that says why the note the user is holding
 * does not apply, not a silent ignore and not a bare "unsupported".
 *
 * This lives in the Windows layer rather than in app/backends/ because it is a
 * decision, not a call: it compiles and is tested on every platform
 * (tests/unit/tst_winbackends.cpp), while the half that has to touch the OS
 * stays behind `if(WIN32)`.
 */
QString windowsClipboardProblem(const QString &forced);

} // namespace lt
