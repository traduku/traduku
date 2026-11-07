#pragma once

#include <QObject>
#include <QString>

class QMenu;

namespace lt {

/*!
 * Tray presence, whatever draws it.
 *
 * One implementation today - `QtTray` (QSystemTrayIcon, which on Wayland is
 * Qt's own generic DBus SNI client; ROADMAP.md §2 records the measurement
 * that chose it). `KdeTray` (KF6 KStatusNotifierItem) is deleted: the Qt tray
 * reproduced its menu entry for entry in a live Plasma session, and KF6's item
 * segfaulted when destroyed with a live session bus, which is what `Quit` did
 * in the released 0.2 build.
 *
 * The interface stays, because three things still depend on it: the shells
 * connect intents once and do not care who draws the icon; `TRADUKU_TRAY=none`
 * swaps in a tray that is not there; and tests/unit/tst_trayparity.cpp pins
 * the menu contract this interface encodes, so a future tray cannot quietly
 * drop an entry or wire `Quit` to QApplication::quit().
 */
class ITray : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITray() override = default;

    /// The menu the host shows on right click. Owned by the tray.
    virtual QMenu *menu() const = 0;

    /// "Working on it". QSystemTrayIcon has no overlay icons, so QtTray puts
    /// puts it in the tooltip; both must surface it somehow.
    virtual void setBusy(bool busy) = 0;
    virtual void setToolTipSub(const QString &sub) = 0;
    virtual void show() = 0;

    /// False when the desktop offers no tray at all (GNOME without an SNI
    /// extension). Running tray-less is supported, not a failure.
    virtual bool isAvailable() const = 0;

    /// For --backend-info: "qt (QSystemTrayIcon)", "none (no tray host)".
    virtual QString backendName() const = 0;

    /// A disabled, informative row at the top of the menu: which build this is
    /// and which tray it drew. The operator restarted the app from the Start
    /// Menu during the Stage 6b comparison and could not say afterwards which
    /// binary had been in the panel - a tray that cannot answer "which build
    /// am I" makes every report about it ambiguous.
    virtual void setStatusLabel(const QString &text) = 0;

Q_SIGNALS:
    void translateRequested();
    void targetLanguagePicked(const QString &code);
    void settingsRequested();
    /// An intent, never QApplication::quit(): the shell decides what leaving
    /// means (and tests can observe it without an event loop).
    void quitRequested();
};

} // namespace lt
