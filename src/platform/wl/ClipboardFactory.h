#pragma once

#include "WlClipboard.h"
#include "interfaces/iclipboard.h"

#include <memory>

class QClipboard;

namespace lt {

/*!
 * Chooses the best clipboard for the running session:
 *   wayland + wl-clipboard tools  -> WlClipboard
 *   everything else (and X11)     -> QtClipboard
 * `platformName` is QGuiApplication::platformName().
 */
std::unique_ptr<IClipboard> createClipboard(
    const QString &platformName,
    const WlClipboard::ToolFinder &finder = {},
    QClipboard *qtClipboard = nullptr,
    const WlClipboard::ToolRunner &runner = {});

} // namespace lt
