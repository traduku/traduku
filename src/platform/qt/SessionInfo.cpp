#include "SessionInfo.h"

#include <QGuiApplication>

namespace lt {

QString sessionType()
{
    const QString qpa = QGuiApplication::platformName().toLower();
    if (qpa.startsWith(QLatin1String("wayland"))) {
        return QStringLiteral("wayland");
    }
    if (qpa == QLatin1String("xcb")) {
        return QStringLiteral("x11");
    }

    // offscreen/minimal/vnc/unknown: the plugin name says nothing about the
    // desktop, so ask the environment.
    const QString xdg = qEnvironmentVariable("XDG_SESSION_TYPE").toLower();
    if (xdg == QLatin1String("wayland") || xdg == QLatin1String("x11")) {
        return xdg;
    }
    if (!qpa.isEmpty() && qpa != QLatin1String("offscreen")
        && qpa != QLatin1String("minimal")) {
        return qpa;                       // e.g. eglfs on an embedded box
    }
    return QString();                     // honestly unknown
}

bool isWaylandSession()
{
    return sessionType() == QLatin1String("wayland");
}

} // namespace lt
