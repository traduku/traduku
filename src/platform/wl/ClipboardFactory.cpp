#include "ClipboardFactory.h"

#include "platform/qt/QtClipboard.h"

#include <QClipboard>
#include <QGuiApplication>

namespace lt {

std::unique_ptr<IClipboard> createClipboard(const QString &platformName,
                                            const WlClipboard::ToolFinder &finder,
                                            QClipboard *qtClipboard,
                                            const WlClipboard::ToolRunner &runner)
{
    if (platformName == QLatin1String("wayland")
        && WlClipboard::isUsable(runner, finder)) {
        return std::make_unique<WlClipboard>(runner, finder);
    }
    if (!qtClipboard) {
        qtClipboard = QGuiApplication::clipboard();
    }
    return std::make_unique<QtClipboard>(qtClipboard);
}

} // namespace lt
