#include "snitypes.h"

#include <QDBusMetaType>
#include <QImage>

namespace lt {

QDBusArgument &operator<<(QDBusArgument &arg, const SniPixmap &pixmap)
{
    arg.beginStructure();                       // (iiay)
    arg << pixmap.width << pixmap.height << pixmap.bytes;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, SniPixmap &pixmap)
{
    arg.beginStructure();
    arg >> pixmap.width >> pixmap.height >> pixmap.bytes;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const LayoutItem &item)
{
    arg.beginStructure();                       // (ia{sv}av)
    arg << item.id << item.properties;
    arg.beginArray(qMetaTypeId<QDBusVariant>());
    for (const LayoutItem &child : item.children) {
        arg << QDBusVariant(QVariant::fromValue(child));
    }
    arg.endArray();
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, LayoutItem &item)
{
    arg.beginStructure();
    arg >> item.id >> item.properties;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusVariant v;
        arg >> v;
        const LayoutItem child = v.variant().value<LayoutItem>();
        item.children.append(child);
    }
    arg.endArray();
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const PropertyPair &pair)
{
    arg.beginStructure();                       // (ia{sv})
    arg << pair.id << pair.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PropertyPair &pair)
{
    arg.beginStructure();
    arg >> pair.id >> pair.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const PropertyPairList &pairs)
{
    arg.beginArray(qMetaTypeId<PropertyPair>());    // a(ia{sv})
    for (const PropertyPair &pair : pairs) {
        arg << pair;
    }
    arg.endArray();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PropertyPairList &pairs)
{
    arg.beginArray();
    while (!arg.atEnd()) {
        PropertyPair pair;
        arg >> pair;
        pairs.append(pair);
    }
    arg.endArray();
    return arg;
}

void registerSniMetaTypes()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    // The container needs registering next to its element type: qDBusRegister-
    // MetaType<PropertyPair>() does not teach Qt how to stream
    // QList<PropertyPair>, and the symptom of forgetting it is identical to a
    // wrong method signature - the host's call is refused and the menu is
    // simply empty, with no error on the host side.
    qRegisterMetaType<SniPixmap>("lt::SniPixmap");
    qRegisterMetaType<SniPixmapVector>("lt::SniPixmapVector");
    qDBusRegisterMetaType<SniPixmap>();
    qDBusRegisterMetaType<SniPixmapVector>();

    qRegisterMetaType<LayoutItem>("lt::LayoutItem");
    qDBusRegisterMetaType<LayoutItem>();

    qRegisterMetaType<PropertyPair>("lt::PropertyPair");
    qRegisterMetaType<PropertyPairList>("lt::PropertyPairList");
    qDBusRegisterMetaType<PropertyPair>();
    qDBusRegisterMetaType<PropertyPairList>();
}

QByteArray toSniArgb32(const QImage &image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QByteArray out(static_cast<qsizetype>(rgba.sizeInBytes()), Qt::Uninitialized);
    char *dst = out.data();
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar *src = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            const uchar r = src[x * 4 + 0], g = src[x * 4 + 1],
                      b = src[x * 4 + 2], a = src[x * 4 + 3];
            *dst++ = a;
            *dst++ = r;
            *dst++ = g;
            *dst++ = b;   // ARGB32, network byte order
        }
    }
    return out;
}

} // namespace lt
