#pragma once

#include <QByteArray>
#include <QDBusArgument>
#include <QImage>
#include <QList>
#include <QMetaType>
#include <QVariantMap>

namespace lt {

// Types the D-Bus specs reference by name. The annotations in
// tools/spike/xml/*.xml must spell these exactly - qdbusxml2cpp copies them
// into the generated signature strings, and a mismatch shows up as the host's
// call being refused ("no slot matched"), i.e. a tray menu that is empty.

/// One SNI IconPixmap / OverlayIconPixmap element, (iiay).
/// The bytes are ARGB32 in *network* byte order - A,R,G,B per pixel, which is
/// NOT QImage::Format_ARGB32's memory order on little-endian machines.
struct SniPixmap
{
    int width = 0;
    int height = 0;
    QByteArray bytes;
};
using SniPixmapVector = QList<SniPixmap>;

/// One dbusmenu layout node, (ia{sv}av).
struct LayoutItem
{
    int id = 0;
    QVariantMap properties;
    QList<LayoutItem> children;
};

/// One GetGroupProperties element, (ia{sv}).
struct PropertyPair
{
    int id = 0;
    QVariantMap properties;
};
using PropertyPairList = QList<PropertyPair>;

QDBusArgument &operator<<(QDBusArgument &arg, const SniPixmap &pixmap);
const QDBusArgument &operator>>(const QDBusArgument &arg, SniPixmap &pixmap);

QDBusArgument &operator<<(QDBusArgument &arg, const LayoutItem &item);
const QDBusArgument &operator>>(const QDBusArgument &arg, LayoutItem &item);

QDBusArgument &operator<<(QDBusArgument &arg, const PropertyPair &pair);
const QDBusArgument &operator>>(const QDBusArgument &arg, PropertyPair &pair);

QDBusArgument &operator<<(QDBusArgument &arg, const PropertyPairList &pairs);
const QDBusArgument &operator>>(const QDBusArgument &arg, PropertyPairList &pairs);

/// Must run before any of these types crosses the bus.
///
/// Both halves are required and neither implies the other in practice:
/// qDBusRegisterMetaType() teaches QDBusArgument how to stream the type,
/// while the *named* qRegisterMetaType() is what Qt's method matcher uses - it
/// resolves a slot's arguments by the type name that appears in the moc'd
/// signature, so the string has to match the C++ spelling exactly (which is
/// also why these types live in a named namespace: inside `namespace {}` the
/// name becomes "QList<{anonymous}::PropertyPair>" and cannot be registered).
void registerSniMetaTypes();

/// QImage -> the (iiay) payload, ARGB32 network byte order.
QByteArray toSniArgb32(const QImage &image);

} // namespace lt

Q_DECLARE_METATYPE(lt::SniPixmap)
Q_DECLARE_METATYPE(lt::SniPixmapVector)
Q_DECLARE_METATYPE(lt::LayoutItem)
Q_DECLARE_METATYPE(lt::PropertyPair)
Q_DECLARE_METATYPE(lt::PropertyPairList)
