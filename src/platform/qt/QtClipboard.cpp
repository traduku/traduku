#include "QtClipboard.h"

#include <QClipboard>

namespace lt {

QtClipboard::QtClipboard(QClipboard *clipboard)
    : m_clipboard(clipboard)
{
}

QString QtClipboard::text() const
{
    return m_clipboard ? m_clipboard->text() : QString();
}

bool QtClipboard::setText(const QString &text)
{
    if (!m_clipboard) {
        return false;
    }
    m_clipboard->setText(text, QClipboard::Clipboard);
    return true;
}

} // namespace lt
