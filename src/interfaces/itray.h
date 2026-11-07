#pragma once

#include <QObject>
#include <QString>

class QMenu;

namespace lt {

/*!
 * Tray presence, whatever draws it.
 *
 * Two implementations exist deliberately during the port: `KdeTray`
 * (KF6 KStatusNotifierItem) and `QtTray` (QSystemTrayIcon, which is Qt's
 * generic DBus SNI client on Wayland - GNOME_PLAN.md D3). The shell holds an
 * ITray*, so switching one against the other is a configuration decision
 * (LT_TRAY / TRADUKU_TRAY) and not an edit in main.cpp - which is what makes
 * the Stage 6b comparison "same binary, two trays" possible.
 *
 * Tests pin the two together (tests/unit/tst_trayparity.cpp): identical menu,
 * identical intents. KdeTray may only be deleted once that test is green
 * against the Qt tray in a real Plasma session.
 */
class ITray : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITray() override = default;

    /// The menu the host shows on right click. Owned by the tray.
    virtual QMenu *menu() const = 0;

    /// "Working on it". KdeTray paints an overlay icon, QtTray has none and
    /// puts it in the tooltip; both must surface it somehow.
    virtual void setBusy(bool busy) = 0;
    virtual void setToolTipSub(const QString &sub) = 0;
    virtual void show() = 0;

    /// False when the desktop offers no tray at all (GNOME without an SNI
    /// extension). Running tray-less is supported, not a failure.
    virtual bool isAvailable() const = 0;

    /// For --backend-info: "kde (KStatusNotifierItem)", "qt (QSystemTrayIcon)".
    virtual QString backendName() const = 0;

Q_SIGNALS:
    void translateRequested();
    void targetLanguagePicked(const QString &code);
    void settingsRequested();
    /// An intent, never QApplication::quit(): the shell decides what leaving
    /// means (and tests can observe it without an event loop).
    void quitRequested();
};

} // namespace lt
