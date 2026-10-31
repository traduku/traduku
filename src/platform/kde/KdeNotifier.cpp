#include "KdeNotifier.h"

#include <KNotification>

namespace lt {

void KdeNotifier::notify(const QString &title, const QString &body,
                         const QString &iconName, int ms)
{
    auto *n = new KNotification(QStringLiteral("llm-translate-notify"),
                                KNotification::CloseOnTimeout, this);
    n->setComponentName(QStringLiteral("llm-translate"));
    n->setTitle(title);
    n->setText(body);
    n->setIconName(iconName.isEmpty() ? QStringLiteral("accessories-dictionary")
                                      : iconName);
    if (ms > 0) {
        n->setFlags(n->flags() | KNotification::CloseOnTimeout);
    }
    n->sendEvent();
    connect(n, &KNotification::closed, n, &QObject::deleteLater);
}

} // namespace lt
