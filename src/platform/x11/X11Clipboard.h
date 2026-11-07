#pragma once

#include "interfaces/iclipboard.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QSocketNotifier;
typedef struct _XDisplay Display;   // Xlib's own spelling

namespace lt {

/*!
 * Where the X server is, when we were not started by it.
 *
 * The case this exists for: the user logs into GNOME or Plasma on Wayland,
 * and the only X server on the machine is the XWayland that desktop launched
 * for X clients. A process started by `systemctl --user` / XDG autostart has
 * no DISPLAY and no XAUTHORITY in its environment - and XWayland's cookie
 * lives in a randomly named file under /run/user/UID/, referenced only by the
 * Xwayland process itself. So both are read from there.
 *
 * Everything here takes its input as data (environment blocks, /proc
 * cmdlines) and is unit-testable without a display; the resolvers below are
 * the thin glue that reads the real /proc.
 */
namespace X11Session {

/// DISPLAY=<value> from an environment as a list of "KEY=VALUE" entries.
QString displayFromEnvironment(const QStringList &envEntries);

/// DISPLAY served by a running Xwayland, from `ps`-style cmdline strings.
/// Empty when no Xwayland is among them.
QString displayOfXwayland(const QStringList &procCmdlines);

/// XAUTHORITY=<value> from a NUL-separated /proc/<pid>/environ block.
QString xauthorityFromEnviron(const QByteArray &environ);

/// Xauthority files worth trying, most specific first, no duplicates:
/// an explicit value, then the display manager's for this uid, then home.
QStringList candidateXauthorities(const QString &home, int uid,
                                  const QString &fromEnvironment);

/// Process cmdlines of every process we may read (argv joined with spaces).
QStringList readProcCmdlines();

/// environ block of a pid, empty when unreadable (another user's process).
QByteArray readProcEnviron(int pid);

/// The two lookups above, wired together: DISPLAY to use, empty if none.
QString resolveDisplay();

/// XAUTHORITY to use for `display`: the cookie of the Xwayland serving it,
/// else the first existing candidate. Empty means "try without authority".
QString resolveXauthority(const QString &display);

} // namespace X11Session

/*!
 * Clipboard through X selections (CLIPBOARD / PRIMARY), for the sessions
 * where Qt's own clipboard sees nothing.
 *
 * Why not QClipboard: a wayland session gives a background client no data
 * offer at all, so Qt reads an empty clipboard (measured, ROADMAP.md §2).
 * The X client inside that session - XWayland - is a normal X client, and
 * there the ICCCM protocol works for a process that never had focus:
 * XGetSelectionOwner to see who owns it, XConvertSelection to ask them, and
 * the reply arrives as a SelectionNotify on our own window.
 *
 * Writing means *becoming* the owner, which is a promise to answer other
 * clients: XSetSelectionOwner only says who we are, and every later
 * SelectionRequest has to be served from whatever we hold. That is why the
 * payload is kept here and why processEvents() exists - a clipboard that can
 * be read once and then hangs is worse than no clipboard, because every
 * other app's paste will sit waiting on us.
 *
 * Decoding is not this class's business: the target list and the bytes go to
 * core/texttargets, shared with the wayland reader.
 */
class X11Clipboard : public QObject, public IClipboard
{
    Q_OBJECT
public:
    struct Options
    {
        QString display;            // empty -> X11Session::resolveDisplay()
        QString xauthority;         // empty -> X11Session::resolveXauthority()
        QString selection;          // "CLIPBOARD" (default), "PRIMARY", or a
                                    // private atom: tests must not overwrite
                                    // the user's clipboard to prove a point
    };

    explicit X11Clipboard(Options options = {});
    ~X11Clipboard() override;

    /// Can we connect at all? The factory asks this before choosing us over
    /// Qt: on a machine with no X server (wayland without XWayland) the right
    /// answer is "no", and Qt keeps its place as the last resort.
    static bool isUsable(const Options &options = {});

    /// Why isUsable() said no ("" when it said yes). --backend-info needs the
    /// reason, not just the verdict: "no X server" and "X server refused us
    /// because of the cookie" are different fixes for the user.
    static QString unusabilityReason(const Options &options = {});

    QString text() const override;
    bool setText(const QString &text) override;
    QString backendName() const override { return QStringLiteral("x11"); }
    QString diagnostic() const override { return m_diagnostic; }

    /// Serve other clients' SelectionRequest/SelectionClear and pick up any
    /// SelectionNotify that arrived. Called by the round trips below, and by
    /// the app from a QSocketNotifier on the connection fd - an owner that
    /// only answers when it happens to be reading is an owner that hangs
    /// other people's paste.
    /// Returns true if at least one event was handled.
    bool processEvents(int timeoutMs = 0) const;

    /// The fd to watch for events (QSocketNotifier), -1 when not connected.
    int connectionFd() const;

private:
    bool openConnection() const;
    /// Ask the current owner for its TARGETS, then for the best one.
    QString requestText(const QString &target) const;
    QByteArray requestProperty(const QString &target, const QString &wantType) const;
    void serveRequest(const void *requestEvent) const;

    Options m_options;
    // Filled in by the const connect(): a failure to reach the server is a
    // diagnostic, and asking for the clipboard is not a mutation of us.
    mutable QString m_diagnostic;
    mutable Display *m_display = nullptr;
    mutable unsigned long m_ownWindow = 0;      // Window, kept Xlib-free
    mutable QByteArray m_payload;               // what we promise to serve
    mutable bool m_owning = false;
    mutable QString m_servedType;
    // The promise, kept: Xlib's connection fd watched by the Qt event loop.
    // Without this the owner answers only while it is itself reading, and
    // every other application's paste waits on a timeout (measured by
    // tst_x11clipboard::theQtEventLoopKeepsUsServing).
    mutable std::unique_ptr<QSocketNotifier> m_notifier;
};

} // namespace lt
