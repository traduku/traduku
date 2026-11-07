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

/*!
 * Why a forced TRADUKU_CLIPBOARD did not take effect, or empty when it did.
 *
 * The knob exists so a user can separate "our reader is broken" from "the
 * compositor is broken". Silently substituting another backend destroys that
 * distinction - forcing x11 on a machine where X is unreachable and getting
 * wl-clipboard with no explanation looks like the knob being ignored (measured
 * on ubuntu-gnome). Stated as a pure function so the wording is tested rather
 * than trusted.
 */
QString forcedBackendProblem(const QString &chosenBackend, const QString &forced,
                             bool forcedIsUsable, const QString &reason);
/// Same, reading TRADUKU_CLIPBOARD and probing what it asked for.
QString forcedBackendProblem(const QString &chosenBackend);

} // namespace lt
