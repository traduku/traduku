#include "ClipboardFactory.h"

#include "platform/qt/QtClipboard.h"

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

std::unique_ptr<IClipboard> createClipboard(const QString &platformName,
                                            const WlClipboard::ToolFinder &finder,
                                            QClipboard *qtClipboard,
                                            const WlClipboard::ToolRunner &runner)
{
    if (platformName == QLatin1String("wayland")) {
        if (WlClipboard::isUsable(runner, finder)) {
            return std::make_unique<WlClipboard>(runner, finder);
        }
        if (!qtClipboard) {
            qtClipboard = QGuiApplication::clipboard();
        }
        // Blind by construction - carry the instruction to install the tools.
        return std::make_unique<QtClipboard>(qtClipboard, missingToolsHint());
    }
    if (!qtClipboard) {
        qtClipboard = QGuiApplication::clipboard();
    }
    return std::make_unique<QtClipboard>(qtClipboard);
}

} // namespace lt
