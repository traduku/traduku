/*
    tools/spike/tray-spike.cpp — Stage 5 pre-work (GNOME_PLAN.md D3).

    Question it answers: which tray protocol can actually paint an icon and a
    menu on THIS desktop?

    That question matters because the usual advice ("just use QSystemTrayIcon,
    Qt picks the right mechanism") is only half true on Linux. Qt's tray on
    X11 speaks the legacy XEmbed SystemTray protocol - the one GNOME Shell
    deleted in 3.26 - and Qt's Wayland platform plugin provides no tray
    provider of its own. What makes it work under GNOME/Wayland anyway is
    Qt's *generic* unix tray, QGenericSystemTray, which speaks
    StatusNotifierItem over D-Bus. Whether that exists on the Qt version of an
    older distribution is a distribution question, so the spike measures it
    instead of trusting either claim.

    Modes:

        tray-spike info              what the desktop offers (watcher, items)
        tray-spike qtsys [N]         QSystemTrayIcon only
        tray-spike sni [N]           our own SNI client + com.canonical.dbusmenu
        tray-spike both [N]          both at once (default) - the decisive run

    In `both` the two items carry different labels, so "which one is on the
    panel" is an observation, not an interpretation.

    The adaptors are GENERATED from the spec XML (see xml/README.md): the first
    version of this file had them by hand and five signatures were wrong, each
    of which showed up only as "the menu never opens".

    Not product code, not a ctest suite, not installed.
*/

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>

#include "menu-adaptor.h"
#include "sni-adaptor.h"
#include "snitypes.h"
#include "spikelog.h"
#include "tray-object.h"

using namespace lt;

namespace {

constexpr auto kItemPath = "/StatusNotifierItem";
constexpr auto kMenuPath = "/MenuBar";
constexpr auto kWatcher = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";

QImage makeIcon(int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal m = size * 0.08;
    p.setBrush(QColor(0x2b, 0x6b, 0xcb));
    p.setPen(QPen(QColor(0xff, 0xff, 0xff), size * 0.06));
    p.drawEllipse(QRectF(m, m, size - 2 * m, size - 2 * m));
    QFont f = p.font();
    f.setPixelSize(int(size * 0.55));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("T"));
    p.end();
    return img;
}

QList<QImage> makeIconImages()
{
    // Panel size and its HiDPI twin; a host picks the one it needs.
    return {makeIcon(22), makeIcon(44)};
}

SniPixmapVector toPixmaps(const QList<QImage> &images)
{
    SniPixmapVector out;
    for (const QImage &img : images) {
        out.append(SniPixmap{img.width(), img.height(), toSniArgb32(img)});
    }
    return out;
}

QIcon toQIcon(const QList<QImage> &images)
{
    QIcon icon;
    for (const QImage &img : images) {
        icon.addPixmap(QPixmap::fromImage(img));
    }
    return icon;
}

QVariant watcherProperty(const QString &name, QDBusConnection &bus)
{
    QDBusInterface w(QStringLiteral("org.freedesktop.DBus"), QString(),
                     QStringLiteral("org.freedesktop.DBus.Properties"), bus);
    QDBusInterface item(QLatin1String(kWatcher), QLatin1String(kWatcherPath),
                        QStringLiteral("org.freedesktop.DBus.Properties"), bus);
    Q_UNUSED(w);
    const QDBusReply<QVariant> r = item.call(QStringLiteral("Get"),
                                             QLatin1String(kWatcher), name);
    if (!r.isValid()) {
        sayErr(QStringLiteral("watcher property %1: %2").arg(name, r.error().message()));
        return {};
    }
    return r.value();
}

void reportWatcher(QDBusConnection &bus)
{
    const bool hasWatcher = bus.interface()
        && bus.interface()->isServiceRegistered(QLatin1String(kWatcher));
    say(QStringLiteral("watcher %1 registered: %2")
            .arg(QLatin1String(kWatcher),
                 hasWatcher ? QStringLiteral("yes") : QStringLiteral("NO")));
    if (!hasWatcher) {
        sayErr(QStringLiteral("no StatusNotifierWatcher on the session bus: no SNI "
                              "host is running, so no SNI icon can appear. On GNOME "
                              "that means no shell extension is enabled."));
        return;
    }
    say(QStringLiteral("IsStatusNotifierHostRegistered: %1")
            .arg(watcherProperty(QStringLiteral("IsStatusNotifierHostRegistered"), bus).toBool()));
    const QStringList items =
        watcherProperty(QStringLiteral("RegisteredStatusNotifierItems"), bus).toStringList();
    say(QStringLiteral("RegisteredStatusNotifierItems: %1").arg(items.size()));
    for (const QString &i : items) {
        say(QStringLiteral("    %1").arg(i));
    }

    // IsStatusNotifierItemRegistered is a method, not a property - the
    // interface is uneven, and reading it as a property fails with
    // "No such property" (measured).
    QDBusInterface w(QLatin1String(kWatcher), QLatin1String(kWatcherPath),
                     QLatin1String(kWatcher), bus);
    const QDBusReply<bool> mine =
        w.call(QStringLiteral("IsStatusNotifierItemRegistered"), QStringLiteral("tray-spike"));
    say(QStringLiteral("IsStatusNotifierItemRegistered(tray-spike): %1")
            .arg(!mine.isValid() ? QStringLiteral("call failed: ") + mine.error().message()
                                 : (mine.value() ? QStringLiteral("yes")
                                                 : QStringLiteral("no"))));
}

// ------------------------------------------------------------- QSystemTray

bool tryQtSystemTray(const QString &iconName, const QList<QImage> &images)
{
    say(QStringLiteral("--- QSystemTrayIcon (what the usual advice suggests) ---"));
    if (QIcon::themeSearchPaths().isEmpty()) {
        QIcon::setThemeSearchPaths({QStringLiteral("/usr/share/icons"),
                                    QStringLiteral("/usr/share/pixmaps")});
    }
    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName(QStringLiteral("Adwaita"));
    }
    if (!iconName.isEmpty() && !QIcon::hasThemeIcon(iconName)) {
        say(QStringLiteral("theme icon '%1' is not in theme '%2' - the icon below "
                           "comes from painted pixels instead (which is what "
                           "Traduku must rely on: it is not installed, so no theme "
                           "on the machine knows its name)")
                .arg(iconName, QIcon::themeName()));
    }

    const bool available = QSystemTrayIcon::isSystemTrayAvailable();
    say(QStringLiteral("isSystemTrayAvailable(): %1   <- Qt's generic unix tray, if any")
            .arg(available ? QStringLiteral("true") : QStringLiteral("false")));

    QIcon icon = (!iconName.isEmpty() && QIcon::hasThemeIcon(iconName))
        ? QIcon::fromTheme(iconName) : toQIcon(images);
    say(QStringLiteral("handing Qt a QIcon: %1 pixmap(s), null=%2")
            .arg(images.size()).arg(icon.isNull() ? QStringLiteral("YES")
                                                  : QStringLiteral("no")));

    // Leaks on purpose: the run ends by quitting the application.
    auto *tray = new QSystemTrayIcon(icon);
    tray->setToolTip(QStringLiteral("Traduku Qt-tray spike"));
    auto *menu = new QMenu();
    menu->addAction(QStringLiteral("Qt tray: log a line"), []() {
        say(QStringLiteral("QSystemTrayIcon menu item activated"));
    });
    menu->addAction(QStringLiteral("Qt tray: quit"), qApp, &QCoreApplication::quit);
    tray->setContextMenu(menu);
    QObject::connect(tray, &QSystemTrayIcon::activated, tray,
                     [](QSystemTrayIcon::ActivationReason reason) {
        say(QStringLiteral("QSystemTrayIcon activated, reason=%1").arg(int(reason)));
    });
    tray->show();
    say(QStringLiteral("isVisible(): %1  => the item whose menu says 'Qt tray' is "
                       "the one Qt drew").arg(tray->isVisible() ? QStringLiteral("true")
                                                                : QStringLiteral("false")));
    return available;
}

// --------------------------------------------------------------- SNI client

bool trySni(const QString &iconName, const QList<QImage> &images, QDBusConnection &bus)
{
    say(QStringLiteral("--- our own StatusNotifierItem (what SniTray will do) ---"));
    registerSniMetaTypes();

    auto *menuObject = new TrayMenu;
    new MenuAdaptor(menuObject);
    if (!bus.registerObject(QLatin1String(kMenuPath), menuObject,
                            QDBusConnection::ExportAdaptors)) {
        sayErr(QStringLiteral("registerObject(%1): %2")
                   .arg(QLatin1String(kMenuPath), bus.lastError().message()));
        return false;
    }

    auto *item = new TrayItem;
    item->iconName = iconName;
    item->title = QStringLiteral("Traduku SNI spike");
    item->pixmaps = toPixmaps(images);
    new SniAdaptor(item);
    if (!bus.registerObject(QLatin1String(kItemPath), item,
                            QDBusConnection::ExportAdaptors)) {
        sayErr(QStringLiteral("registerObject(%1): %2")
                   .arg(QLatin1String(kItemPath), bus.lastError().message()));
        return false;
    }
    say(QStringLiteral("IconPixmap: %1 entries, theme-independent; IconName: '%2'")
            .arg(item->pixmaps.size()).arg(iconName));

    QDBusInterface watcher(QLatin1String(kWatcher), QLatin1String(kWatcherPath),
                           QLatin1String(kWatcher), bus);
    if (!watcher.isValid()) {
        sayErr(QStringLiteral("watcher interface unusable: %1").arg(watcher.lastError().message()));
        return false;
    }
    const QDBusReply<void> reg = watcher.call(QDBus::Block,
                                              QStringLiteral("RegisterStatusNotifierItem"),
                                              bus.baseService());
    if (!reg.isValid()) {
        sayErr(QStringLiteral("RegisterStatusNotifierItem(%1): %2")
                   .arg(bus.baseService(), reg.error().message()));
        return false;
    }
    say(QStringLiteral("registered as %1 at %2, menu at %3")
           .arg(bus.baseService(), QLatin1String(kItemPath), QLatin1String(kMenuPath)));
    say(QStringLiteral("=> the item whose menu root says 'Traduku spike' is ours"));
    return true;
}

/// Qt 6 has no functor overload of QDBusConnection::connect, so the signal
/// needs a real slot on a QObject.
class HostLifecycle : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

public slots:
    void nameOwnerChanged(const QString &name, const QString &oldOwner,
                          const QString &newOwner)
    {
        if (name != QLatin1String(kWatcher)) {
            return;
        }
        if (newOwner.isEmpty()) {
            say(QStringLiteral("watcher vanished (was %1): an SNI client must "
                               "re-register when a host comes back").arg(oldOwner));
        } else {
            say(QStringLiteral("watcher appeared as %1: re-register now").arg(newOwner));
        }
    }
};

void watchHostLifecycle(QDBusConnection &bus)
{
    static HostLifecycle *watcher = new HostLifecycle;
    if (!bus.connect(QStringLiteral("org.freedesktop.DBus"),
                     QStringLiteral("/org/freedesktop/DBus"),
                     QStringLiteral("org.freedesktop.DBus"),
                     QStringLiteral("NameOwnerChanged"),
                     watcher, SLOT(nameOwnerChanged(QString,QString,QString)))) {
        sayErr(QStringLiteral("NameOwnerChanged watch: %1").arg(bus.lastError().message()));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tray-spike"));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Which tray protocol actually shows an icon here?"));
    parser.addHelpOption();
    QCommandLineOption iconOpt({QStringLiteral("i"), QStringLiteral("icon")},
                               QStringLiteral("Themed icon name to use when it exists."),
                               QStringLiteral("name"));
    QCommandLineOption secondsOpt(
        {QStringLiteral("s"), QStringLiteral("seconds")},
        QStringLiteral("Exit after N seconds; 0 = run until killed (default 90)."),
        QStringLiteral("n"), QStringLiteral("90"));
    QCommandLineOption noPixmapOpt(
        QStringLiteral("no-pixmap"),
        QStringLiteral("Send only IconName (reproduces the '...' fallback icon)."));
    parser.addOption(iconOpt);
    parser.addOption(secondsOpt);
    parser.addOption(noPixmapOpt);
    parser.addPositionalArgument(QStringLiteral("mode"),
                                 QStringLiteral("info | qtsys | sni | both (default both)"),
                                 QStringLiteral("[mode]"));
    parser.process(app);

    const QString mode = parser.positionalArguments().isEmpty()
        ? QStringLiteral("both") : parser.positionalArguments().first();
    const QString iconName = parser.value(iconOpt);
    const int seconds = parser.value(secondsOpt).toInt();

    say(QStringLiteral("platform plugin: %1  |  session: %2  |  icon theme: %3  |  Qt %4")
            .arg(QGuiApplication::platformName(),
                 qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("?")),
                 QIcon::themeName().isEmpty() ? QStringLiteral("(none)") : QIcon::themeName(),
                 QStringLiteral(QT_VERSION_STR)));

    QDBusConnection bus = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, QStringLiteral("tray-spike"));
    if (!bus.isConnected()) {
        sayErr(QStringLiteral("session bus: %1").arg(bus.lastError().message()));
        return 2;
    }
    say(QStringLiteral("session bus name: %1").arg(bus.baseService()));

    reportWatcher(bus);
    if (mode == QLatin1String("info")) {
        return 0;
    }

    const QList<QImage> images = makeIconImages();

    bool ok = false;
    if (mode == QLatin1String("qtsys")) {
        ok = tryQtSystemTray(iconName, images);
    } else if (mode == QLatin1String("sni")) {
        ok = trySni(iconName, parser.isSet(noPixmapOpt) ? QList<QImage>() : images, bus);
    } else if (mode == QLatin1String("both")) {
        const bool qtOk = tryQtSystemTray(iconName, images);
        const bool sniOk = trySni(iconName,
                                  parser.isSet(noPixmapOpt) ? QList<QImage>() : images, bus);
        say(QStringLiteral("summary: Qt tray %1, our SNI %2")
                .arg(qtOk ? QStringLiteral("available") : QStringLiteral("unavailable"),
                     sniOk ? QStringLiteral("registered") : QStringLiteral("rejected")));
        ok = sniOk;
    } else {
        sayErr(QStringLiteral("unknown mode '%1' (info|qtsys|sni|both)").arg(mode));
        return 2;
    }

    watchHostLifecycle(bus);
    if (seconds > 0) {
        QTimer::singleShot(seconds * 1000, &app, []() {
            say(QStringLiteral("timeout reached"));
            QCoreApplication::quit();
        });
    }
    say(QStringLiteral("look at the panel: left-click should log Activate and flip "
                       "the icon, right-click should open the menu. Ctrl+C also quits."));
    return app.exec() | (ok ? 0 : 1);
}

#include "tray-spike.moc"
