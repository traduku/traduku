#pragma once

#include "interfaces/iclipboard.h"

class QClipboard;

namespace lt {

/// X11/native Qt clipboard via the platform plugin (also serves as the
/// fallback everywhere the Wayland tools are missing).
class QtClipboard : public IClipboard
{
public:
    explicit QtClipboard(QClipboard *clipboard);

    QString text() const override;
    bool setText(const QString &text) override;
    QString backendName() const override { return QStringLiteral("qt"); }

private:
    QClipboard *m_clipboard;
};

} // namespace lt
