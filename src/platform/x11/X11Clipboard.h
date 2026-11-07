#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

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

} // namespace lt
