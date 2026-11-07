#pragma once

#include <QString>

namespace lt {

/*!
 * The session the user is actually in: "wayland", "x11" or empty when
 * nothing says.
 *
 * NOT the same as QGuiApplication::platformName(), which is the plugin name:
 * CI, `--selftest` and any headless run force QT_QPA_PLATFORM=offscreen, and
 * a Wayland box would then look like it has no Wayland - which silently
 * picks the Qt clipboard (blind on Wayland) and the wrong gesture backend.
 * Backends must be chosen by session, so this consults the plugin first and
 * falls back to $XDG_SESSION_TYPE.
 */
QString sessionType();

bool isWaylandSession();

} // namespace lt
