#pragma once

#include <QObject>

class QTimer;

namespace lt {

class IHotkeyBackend;
class IClipboard;
class INotifier;
class SoundPlayer;
class Settings;
class ChunkedTranslator;
class TranslatorWindow;

/*!
 * DE-agnostic composition root: wires the core pipeline to the window,
 * clipboard, notifier and sounds. A KDE / GNOME / ... shell only has to
 * build the backends and construct this.
 *
 * Flow:
 *   hotkey activated -> clipboard read -> pipeline.setSource ->
 *     rows laid out, units stream row by row (rowStreaming/rowCommitted),
 *     allDone: done cue + clipboard copy + preview notification
 *   user types in the source pane -> debounced (AutoTranslateDelay) ->
 *     pipeline.applyEdit: LCS diff, only touched rows retranslated
 *   Ctrl+Enter -> applyEdit immediately (ignores the autoTranslate switch)
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
        ChunkedTranslator *pipeline = nullptr;
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
    QTimer *m_debounce = nullptr;
};

} // namespace lt
