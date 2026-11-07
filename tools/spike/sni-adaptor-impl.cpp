// Implementation of the members declared by the GENERATED sni-adaptor.h.
//
// Only the header is generated (qdbusxml2cpp, see xml/README for the exact
// command); the bodies live here on purpose. If the spec XML is regenerated
// and a signature changes, this file stops compiling instead of quietly
// advertising a method no host can call - which is precisely the failure mode
// that hand-written adaptors hide ("couldn't handle call to X: no slot
// matched", and the operator sees an empty menu).

#include "sni-adaptor.h"

#include "spikelog.h"
#include "tray-object.h"

namespace lt {

namespace {
TrayItem *asItem(const QObject *parent)
{
    return qobject_cast<TrayItem *>(const_cast<QObject *>(parent));
}
} // namespace

SniAdaptor::SniAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

SniAdaptor::~SniAdaptor() = default;

QString SniAdaptor::attentionIconName() const
{
    return QString();
}

QString SniAdaptor::attentionMovieName() const
{
    return QString();
}

QString SniAdaptor::category() const
{
    return QStringLiteral("ApplicationStatus");
}

QString SniAdaptor::iconName() const
{
    return asItem(parent()) ? asItem(parent())->iconName : QString();
}

SniPixmapVector SniAdaptor::iconPixmap() const
{
    const TrayItem *i = asItem(parent());
    return i ? i->currentPixmaps() : SniPixmapVector();
}

QString SniAdaptor::iconThemePath() const
{
    return QString();
}

QString SniAdaptor::id() const
{
    return QStringLiteral("tray-spike");
}

bool SniAdaptor::itemIsMenu() const
{
    const TrayItem *i = asItem(parent());
    return i ? i->itemIsMenu : true;
}

QDBusObjectPath SniAdaptor::menu() const
{
    const TrayItem *i = asItem(parent());
    return QDBusObjectPath(i ? i->menuPath : QStringLiteral("/MenuBar"));
}

QString SniAdaptor::overlayIconName() const
{
    return QString();
}

QString SniAdaptor::status() const
{
    return asItem(parent()) ? asItem(parent())->status
                                  : QStringLiteral("Active");
}

QString SniAdaptor::title() const
{
    return asItem(parent()) ? asItem(parent())->title : QString();
}

int SniAdaptor::windowId() const
{
    return 0;
}

void SniAdaptor::Activate(int x, int y)
{
    say(QStringLiteral("SNI: Activate(%1,%2)").arg(x).arg(y));
    if (TrayItem *i = asItem(parent())) {
        i->activated();
    }
}

void SniAdaptor::ContextMenu(int x, int y)
{
    if (TrayItem *i = asItem(parent())) {
        i->contextMenuRequested(x, y);
    }
}

void SniAdaptor::ProvideXdgActivationToken(const QString &token)
{
    // Hosts hand over a focus token before Activate so the client can raise
    // its window against the right seat. SniTray will pass it to the window;
    // the spike only has to show that the call exists.
    say(QStringLiteral("SNI: ProvideXdgActivationToken('%1...')").arg(token.left(8)));
}

void SniAdaptor::Scroll(int delta, const QString &orientation)
{
    say(QStringLiteral("SNI: Scroll(delta=%1, %2)").arg(delta).arg(orientation));
}

void SniAdaptor::SecondaryActivate(int x, int y)
{
    say(QStringLiteral("SNI: SecondaryActivate(%1,%2)").arg(x).arg(y));
}

void SniAdaptor::XAyatanaSecondaryActivate(uint timestamp)
{
    say(QStringLiteral("SNI: XAyatanaSecondaryActivate(ts=%1)").arg(timestamp));
}

} // namespace lt
