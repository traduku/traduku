#include "BackendInfo.h"

namespace lt {

QString formatBackendInfo(const BackendReport &r)
{
    QStringList lines;
    const auto add = [&lines](const QString &key, const QString &value) {
        lines << QStringLiteral("%1: %2").arg(key, value);
    };

    add(QStringLiteral("shell"), r.shell.isEmpty() ? QStringLiteral("unknown") : r.shell);
    add(QStringLiteral("session"),
        r.sessionType.isEmpty() ? QStringLiteral("unknown") : r.sessionType);
    add(QStringLiteral("desktop"),
        r.desktop.isEmpty() ? QStringLiteral("unknown") : r.desktop);
    add(QStringLiteral("clipboard"),
        r.clipboard.isEmpty() ? QStringLiteral("none") : r.clipboard);
    if (!r.clipboardHint.isEmpty()) {
        // Verbatim from the backend (IClipboard::diagnostic) - it is the
        // one that knows what to install.
        add(QStringLiteral("clipboard hint"), r.clipboardHint);
    }

    add(QStringLiteral("gesture"),
        r.gestureSource.isEmpty() ? QStringLiteral("none") : r.gestureSource);
    for (const HotkeyAttempt &a : r.hotkeyAttempts) {
        add(QStringLiteral("hotkey %1").arg(a.name), a.result);
    }
    add(QStringLiteral("hotkey chosen"),
        r.hotkeyChosen.isEmpty() ? QStringLiteral("none") : r.hotkeyChosen);

    if (!r.inputAccess.isEmpty()) {
        add(QStringLiteral("input"), r.inputAccess);
        // The remedy, spelled out - the two traps (rule must sort before
        // 71-seat.rules, trigger needs --action=add) cost an afternoon each.
        add(QStringLiteral("input fix"),
            QStringLiteral("install packaging/udev/70-traduku-uaccess.rules into "
                           "/etc/udev/rules.d (sudo install -Dm644 ... ), then "
                           "sudo udevadm control --reload && sudo udevadm trigger "
                           "--action=add --subsystem-match=input; adding the user "
                           "to the 'input' group is the wrong fix"));
    }

    if (!r.notifier.isEmpty()) {
        add(QStringLiteral("notifier"), r.notifier);
    }
    if (!r.tray.isEmpty()) {
        add(QStringLiteral("tray"), r.tray);
    }
    if (!r.audio.isEmpty()) {
        add(QStringLiteral("audio"), r.audio);
    }
    if (!r.reasoning.isEmpty()) {
        add(QStringLiteral("reasoning"), r.reasoning);
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace lt
