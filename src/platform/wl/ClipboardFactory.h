#pragma once

#include "WlClipboard.h"
#include "interfaces/iclipboard.h"

#include <functional>
#include <memory>

class QClipboard;
class QString;

namespace lt {

/*!
 * Chooses the best clipboard for the running session:
 *   wayland + wl-clipboard tools  -> WlClipboard
 *   any session + a live X server -> X11Clipboard
 *   everything else               -> QtClipboard
 * `platformName` is QGuiApplication::platformName().
 *
 * `xAvailable` answers "is there an X server we could talk to". It is a
 * parameter for the same reason `finder` is: the answer belongs to the
 * machine, and a test that asks the machine cannot tell a wrong choice from
 * an unusual machine. Defaults to probing DISPLAY/XWayland for real.
 */
using XAvailability = std::function<bool()>;

std::unique_ptr<IClipboard> createClipboard(
    const QString &platformName,
    const WlClipboard::ToolFinder &finder = {},
    QClipboard *qtClipboard = nullptr,
    const WlClipboard::ToolRunner &runner = {},
    const XAvailability &xAvailable = {});

/*! The decision, as a function of the three facts that make it. Exposed so
 * it can be tested without owning a session: see the comment in the .cpp. */
enum class ClipboardBackend
{
    WlClipboardTool,
    X11,
    Qt
};
ClipboardBackend chooseClipboard(bool waylandSession, bool haveWlClipboardTools,
                                 bool haveX11Server, const QString &forced);

} // namespace lt
