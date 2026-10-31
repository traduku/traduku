#pragma once

#include <QObject>

namespace lt {

class IHotkeyBackend;
class IClipboard;
class INotifier;
class SoundPlayer;
class Settings;
class TranslationEngine;
class TranslatorWindow;

/*!
 * DE-agnostic composition root: wires the core pipeline to the window,
 * clipboard, notifier and sounds. A KDE / GNOME / ... shell only has to
 * build the backends and construct this.
 *
 * Flow (mirrors the prototype, plus the streaming window):
 *   hotkey activated -> clipboard read -> engine.translate ->
 *   started:  show window (source|target), play "start" cue
 *   delta:    append to the live translation pane
 *   finished: mark done, copy to clipboard (if enabled), play "done" cue,
 *             notify with a preview
 *   failed:   status + error cue + notification
 */
class AppController : public QObject
{
    Q_OBJECT
public:
    struct Backends {
        Settings *settings = nullptr;
        IClipboard *clipboard = nullptr;
        INotifier *notifier = nullptr;
        SoundPlayer *sound = nullptr;
        TranslatorWindow *window = nullptr;
        TranslationEngine *engine = nullptr;
    };

    explicit AppController(const Backends &b, QObject *parent = nullptr);

    /// Ownership stays with the caller; the app lives as long as its backends.
    void setHotkey(IHotkeyBackend *hotkey);

    bool translating() const;

public slots:
    /// The one-shot entry point (hotkey, tray click, DBus, CLI activation).
    void translateNow();

Q_SIGNALS:
    /// Emitted when the window's Settings button is clicked; the shell
    /// opens its settings dialog (dialog lives in the shell: the window
    /// itself stays free of persistence).
    void settingsRequested();

private:
    Backends m_b;
};

} // namespace lt
