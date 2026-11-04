#pragma once

#include "interfaces/iclipboard.h"

class QClipboard;

namespace lt {

/// X11/native Qt clipboard via the platform plugin (also serves as the
/// fallback everywhere the Wayland tools are missing).
///
/// On Wayland a background application is NOT able to read the selection
/// through Qt (no data offer without focus - measured on GNOME 50), so the
/// factory passes a `diagnostic` here when it falls back to Qt there: the
/// app shows it instead of claiming the clipboard is empty.
class QtClipboard : public IClipboard
{
public:
    explicit QtClipboard(QClipboard *clipboard, QString diagnostic = {});

    QString text() const override;
    bool setText(const QString &text) override;
    QString backendName() const override { return QStringLiteral("qt"); }
    QString diagnostic() const override { return m_diagnostic; }

private:
    QClipboard *m_clipboard;
    QString m_diagnostic;
};

} // namespace lt
