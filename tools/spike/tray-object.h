#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "snitypes.h"

namespace lt {

/// The SNI item's state. The generated SniAdaptor is a QDBusAbstractAdaptor
/// whose parent is this object, so the adaptor's bodies (sni-adaptor-impl.cpp)
/// read and mutate state here - the generated header stays untouched and the
/// D-Bus contract stays exactly what the spec XML says.
class TrayItem : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    QString iconName;
    QString title;
    QString status = QStringLiteral("Active");
    SniPixmapVector pixmaps;
    QString menuPath = QStringLiteral("/MenuBar");

    /// What the host's Activate() should do, and how visible the round trip
    /// is: the status flips so the change is on screen, not only in the log.
    void activated();
    void contextMenuRequested(int x, int y);
};

/// The dbusmenu model: one root with two items, enough to tell "the menu
/// rendered" from "the host fetched an empty menu".
class TrayMenu : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    struct Node
    {
        int id;
        QString label;
    };

    QList<Node> nodes = {
        Node{1, QStringLiteral("Traduku spike")},
        Node{2, QStringLiteral("Spike: log a line")},
        Node{3, QStringLiteral("Spike: quit")},
    };
    uint revision = 1;

    const Node *find(int id) const;
    void clicked(int id);
};

} // namespace lt
