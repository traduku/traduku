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
    /// One line per cue of the sound profile - "started: <file>", or the
    /// advice for the cue that cannot resolve. A profile of three cues that
    /// prints one line cannot tell "the start cue is silent" from "the finish
    /// cue is silent", and the report is what a user with no sound reads.
    QStringList audioCues;
    /// Whether the app comes back at login, and who decided: the package
    /// (/etc/xdg/autostart) or the user (~/.config/autostart).
    QString autostart;
    /// What we asked the model to do about its hidden reasoning channel
    /// ("skipped (request options sent)" / "NOT disabled - ..."). Empty when
    /// the shell has no LLM path at all.
    QString reasoning;
};

/// "key: value" per line. Only non-empty fields are printed, so a healthy
/// machine produces no advice at all.
QString formatBackendInfo(const BackendReport &r);

} // namespace lt
