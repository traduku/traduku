// Implementation of the members declared by the GENERATED menu-adaptor.h -
// same reason as in sni-adaptor-impl.cpp: the spec XML stays the contract.
//
// The two signatures that were wrong when written by hand, and what the spec
// actually says (measured against the host with dbus-monitor):
//
//   GetGroupProperties(in ai ids, in as propertyNames) -> a(ia{sv})   <- two
//       IN arguments, ids first; the hand-written versions had one, or had
//       them the other way round.
//   Event(in i id, in s eventId, in v data, in u timestamp)  <- id is int32,
//       not a variant.

#include "menu-adaptor.h"

#include "spikelog.h"
#include "tray-object.h"

namespace lt {

namespace {

TrayMenu *asMenu(const QObject *parent)
{
    return qobject_cast<TrayMenu *>(const_cast<QObject *>(parent));
}

QVariantMap itemProperties(const TrayMenu::Node &node, bool isRoot)
{
    QVariantMap props;
    props.insert(QStringLiteral("label"), node.label);
    props.insert(QStringLiteral("enabled"), true);
    props.insert(QStringLiteral("visible"), true);
    props.insert(QStringLiteral("type"), QStringLiteral("standard"));
    if (isRoot) {
        props.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
        props.remove(QStringLiteral("label"));
    }
    return props;
}

} // namespace

MenuAdaptor::MenuAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

MenuAdaptor::~MenuAdaptor() = default;

QStringList MenuAdaptor::iconThemePath() const
{
    return QStringList();
}

QString MenuAdaptor::status() const
{
    return QStringLiteral("normal");
}

QString MenuAdaptor::textDirection() const
{
    return QStringLiteral("ltr");
}

uint MenuAdaptor::version() const
{
    return 3;
}

void MenuAdaptor::AboutToShow(int id)
{
    say(QStringLiteral("dbusmenu: AboutToShow(%1)").arg(id));
}

QList<int> MenuAdaptor::AboutToShowGroup(const QList<int> &ids, QList<int> &idErrors)
{
    Q_UNUSED(idErrors);
    say(QStringLiteral("dbusmenu: AboutToShowGroup(%1 ids)").arg(ids.size()));
    return {};
}

void MenuAdaptor::Event(int id, const QString &eventId, const QDBusVariant &data,
                        uint timestamp)
{
    Q_UNUSED(data);
    say(QStringLiteral("dbusmenu: Event(id=%1, %2, ts=%3)").arg(id).arg(eventId).arg(timestamp));
    if (eventId == QLatin1String("clicked")) {
        if (TrayMenu *m = asMenu(parent())) {
            m->clicked(id);
        }
    }
}

PropertyPairList MenuAdaptor::GetGroupProperties(const QList<int> &ids,
                                                 const QStringList &propertyNames)
{
    say(QStringLiteral("dbusmenu: GetGroupProperties(%1 ids, %2 property names)")
            .arg(ids.size()).arg(propertyNames.size()));
    PropertyPairList out;
    TrayMenu *m = asMenu(parent());
    if (!m) {
        return out;
    }
    for (int id : ids) {
        const TrayMenu::Node *node = m->find(id);
        if (!node) {
            continue;
        }
        out.append(PropertyPair{id, itemProperties(*node, node->id == 1)});
    }
    return out;
}

uint MenuAdaptor::GetLayout(int parentId, int recursionDepth,
                            const QStringList &propertyNames, LayoutItem &layout)
{
    say(QStringLiteral("dbusmenu: GetLayout(parent=%1, depth=%2, names=%3)")
            .arg(parentId).arg(recursionDepth).arg(propertyNames.join(QLatin1Char(','))));
    TrayMenu *m = asMenu(parent());
    if (!m || (parentId != 0 && parentId != 1)) {
        layout = LayoutItem{};
        return m ? m->revision : 0;
    }

    layout = LayoutItem{};
    layout.id = 1;
    if (const TrayMenu::Node *root = m->find(1)) {
        layout.properties = itemProperties(*root, true);
    }
    if (recursionDepth != 0) {
        for (const TrayMenu::Node &node : m->nodes) {
            if (node.id == 1) {
                continue;
            }
            LayoutItem child;
            child.id = node.id;
            child.properties = itemProperties(node, false);
            layout.children << child;
        }
    }
    return m->revision;
}

QDBusVariant MenuAdaptor::GetProperty(int id, const QString &name)
{
    say(QStringLiteral("dbusmenu: GetProperty(%1, %2)").arg(id).arg(name));
    const TrayMenu *m = asMenu(parent());
    if (!m) {
        return QDBusVariant();
    }
    const TrayMenu::Node *node = m->find(id);
    if (!node) {
        return QDBusVariant();
    }
    return QDBusVariant(itemProperties(*node, node->id == 1).value(name));
}

void MenuAdaptor::SetProperty(int id, const QString &name, const QDBusVariant &value)
{
    say(QStringLiteral("dbusmenu: SetProperty(%1, %2)").arg(id).arg(name));
    Q_UNUSED(value);
}

} // namespace lt
