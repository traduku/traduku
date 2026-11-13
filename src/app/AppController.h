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

    /*! The destination language picked in a control: the window's dropdown, the
     * tray's "Translate To". Both fire the same signal and mean the same thing,
     * so both come here - one owner for the consequence.
     *
     * Saves the setting, then translates what the source pane holds, the way the
     * double-Ctrl+C gesture does: a language chosen over a pane full of text is
     * a question about that text, and "in the next translation" is not an answer
     * (ROADMAP.md §7.8 item 4). An empty pane is not a question: the code is
     * saved and no request is made. Re-translates through setSource, not
     * applyEdit - applyEdit keeps the translations of chunks that did not
     * change, and a language change changes no chunk, so a fix built on it would
     * be a dropdown that still does nothing.
     */
    void pickTargetLanguage(const QString &code);

    /// Bring the window to the front. Connected to SingleInstance's
    /// activateRequested (and to KDBusService::Activate on Plasma): the second
    /// copy prints "asked it to surface its window", and a message that asks
    /// nothing is worse than silence - the user who clicked an icon and saw
    /// nothing has no way to know which of the two happened.
    void surfaceWindow();

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
