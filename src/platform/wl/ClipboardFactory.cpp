#include "ClipboardFactory.h"

#include "platform/qt/QtClipboard.h"
#include "platform/x11/X11Clipboard.h"

#include <QClipboard>
#include <QGuiApplication>

namespace lt {

namespace {

// Measured on GNOME 50 (ROADMAP.md §2): a background wayland client
// receives no data offer, so Qt's clipboard reads nothing there - only
// wl-paste/wl-copy (or an XWayland connection) see the DE clipboard. A
// silently empty result blames the user's clipboard; say what is missing.
// Keep the actionable part in front: notification servers truncate.
QString missingToolsHint()
{
    return QStringLiteral(
        "Traduku cannot see the clipboard: the wl-clipboard tools are not "
        "installed (sudo apt install wl-clipboard, or pacman -S / dnf "
        "install wl-clipboard), and on Wayland a background app cannot read "
        "it through Qt. Install them and restart Traduku.");
}

} // namespace

ClipboardBackend chooseClipboard(bool waylandSession, bool haveWlClipboardTools,
                                bool haveX11Server, const QString &forced)
{
    // The order is the whole product decision, so it is stated as a function
    // of its inputs rather than buried in side effects. A test can write all
    // eight rows; it cannot write the machine it happens to run on.
    //
    // On wayland wl-clipboard comes first: the compositor owns the clipboard
    // and XWayland only holds a bridge of it. Without the tools - the Arch
    // case, where wl-clipboard lives only in the AUR - X11 still beats Qt,
    // because Qt's own xcb backend was measured turning a plain TEXT offer
    // into mojibake while our X11 reader negotiates the way wl-clipboard
    // does, so the two agree on what the selection actually says.
    //
    // A forced backend is honoured only when its prerequisite exists:
    // TRADUKU_CLIPBOARD is a user's debugging knob, and honoring it into a
    // backend that cannot work would produce an empty paste with no reason.
    const QString forcedLower = forced.toLower();
    if (forcedLower == QLatin1String("wl") && haveWlClipboardTools) {
        return ClipboardBackend::WlClipboardTool;
    }
    if (forcedLower == QLatin1String("x11") && haveX11Server) {
        return ClipboardBackend::X11;
    }
    if (forcedLower == QLatin1String("qt")) {
        return ClipboardBackend::Qt;
    }
    if (waylandSession && haveWlClipboardTools) {
        return ClipboardBackend::WlClipboardTool;
    }
    if (haveX11Server) {
        return ClipboardBackend::X11;
    }
    return ClipboardBackend::Qt;
}

std::unique_ptr<IClipboard> createClipboard(const QString &platformName,
                                            const WlClipboard::ToolFinder &finder,
                                            QClipboard *qtClipboard,
                                            const WlClipboard::ToolRunner &runner,
                                            const XAvailability &xAvailable)
{
    const bool wayland = (platformName == QLatin1String("wayland"));
    const bool haveTools = WlClipboard::isUsable(runner, finder);
    const bool haveX11 = xAvailable ? xAvailable() : X11Clipboard::isUsable();

    switch (chooseClipboard(wayland, haveTools, haveX11,
                            qEnvironmentVariable("TRADUKU_CLIPBOARD"))) {
    case ClipboardBackend::WlClipboardTool:
        return std::make_unique<WlClipboard>(runner, finder);
    case ClipboardBackend::X11:
        return std::make_unique<X11Clipboard>();
    case ClipboardBackend::Qt:
        break;
    }

    if (!qtClipboard) {
        qtClipboard = QGuiApplication::clipboard();
    }
    if (wayland) {
        // Blind by construction - carry the instruction to install the tools.
        return std::make_unique<QtClipboard>(qtClipboard, missingToolsHint());
    }
    return std::make_unique<QtClipboard>(qtClipboard);
}

} // namespace lt
