#include "KdeGlobalShortcut.h"

#include <KGlobalAccel>

#include <QAction>
#include <QKeySequence>

namespace lt {

KdeGlobalShortcut::KdeGlobalShortcut(QObject *parent)
    : IGlobalShortcut(parent)
{
    m_action = new QAction(QStringLiteral("Translate Clipboard"), this);
    m_action->setObjectName(QStringLiteral("translate_clipboard"));
    connect(m_action, &QAction::triggered, this, &IGlobalShortcut::triggered);
}

bool KdeGlobalShortcut::registerShortcut(const QString &seq)
{
    const QKeySequence keys(seq);
    if (keys.isEmpty()) {
        return false;
    }
    KGlobalAccel::self()->setGlobalShortcut(m_action, keys);
    return true;
}

void KdeGlobalShortcut::unregister()
{
    KGlobalAccel::self()->removeAllShortcuts(m_action);
}

} // namespace lt
