#pragma once

#include <QList>
#include <QString>

namespace lt {

/// One hotkey backend that was tried, and why it was taken or dropped
/// ("chosen", "unavailable on wayland", "portal.Error.NotAllowed: ...").
struct HotkeyAttempt
{
    QString name;
    QString result;
};

/// Everything an operator needs to answer "why does nothing happen when I
/// press Ctrl+C twice?". Deliberately a plain struct: collecting it needs
/// the live environment, rendering it must be pure so the wording - the
/// actionable part - is unit-testable (tests/unit/tst_backendinfo.cpp) and
/// greppable by tests/e2e.
struct BackendReport
{
    QString shell;            // "gnome" | "kde"
    QString sessionType;      // "wayland" | "x11"
    QString desktop;          // $XDG_CURRENT_DESKTOP
    QString clipboard;        // IClipboard::backendName()
    QString clipboardHint;    // IClipboard::diagnostic(), verbatim
    QList<HotkeyAttempt> hotkeyAttempts;
    QString hotkeyChosen;
    QString gestureSource;    // e.g. "evdev /dev/input/event*" or "gsettings"
    QString inputAccess;      // empty = fine; else what is wrong
    QString notifier;
    QString tray;
    QString audio;
};

/// "key: value" per line. Only non-empty fields are printed, so a healthy
/// machine produces no advice at all.
QString formatBackendInfo(const BackendReport &r);

} // namespace lt
