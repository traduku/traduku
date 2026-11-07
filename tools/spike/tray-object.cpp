#include "tray-object.h"

#include <QCoreApplication>

#include "sni-adaptor.h"
#include "spikelog.h"

namespace lt {

void TrayItem::activated()
{
    // Flipping the status is the visible half of the round trip: NewStatus
    // makes the host re-read the property, so a click changes something on
    // screen instead of only writing to a log nobody is watching.
    status = status == QLatin1String("Active") ? QStringLiteral("NeedsAttention")
                                              : QStringLiteral("Active");
    if (auto *adaptor = findChild<SniAdaptor *>()) {
        // Both signals matter: hosts cache the properties and only re-read them
        // when told. Status alone is invisible on GNOME (measured - this host
        // does not paint NeedsAttention), so the icon changes too, which makes
        // "did my click reach the app?" answerable from the panel alone.
        Q_EMIT adaptor->NewStatus(status);
        Q_EMIT adaptor->NewIcon();
    }
    say(QStringLiteral("SNI: status -> %1, icon should now be %2")
            .arg(status, status == QLatin1String("NeedsAttention")
                          ? QStringLiteral("green") : QStringLiteral("blue")));
}

void TrayItem::contextMenuRequested(int x, int y)
{
    say(QStringLiteral("SNI: ContextMenu(%1,%2) - the host will now read the menu")
            .arg(x).arg(y));
}

const TrayMenu::Node *TrayMenu::find(int id) const
{
    for (const Node &n : nodes) {
        if (n.id == id) {
            return &n;
        }
    }
    return nullptr;
}

void TrayMenu::clicked(int id)
{
    const Node *n = find(id);
    say(QStringLiteral("dbusmenu: activated id=%1 (%2)")
            .arg(id).arg(n ? n->label : QStringLiteral("unknown id")));
    if (id == 3) {
        say(QStringLiteral("menu asked to quit"));
        QCoreApplication::quit();
    }
}

} // namespace lt
