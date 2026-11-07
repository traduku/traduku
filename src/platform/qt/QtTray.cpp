#include "QtTray.h"

#include "core/languages.h"
#include "core/settings.h"
#include "interfaces/iautostart.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <cstdio>
static void probe(const char*s){fprintf(stderr,"PROBE QtTray %s\n",s);fflush(stderr);}
#include <QMetaEnum>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>

namespace lt {

namespace {

QImage paintedIcon(int size)
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

static const char *reasonName(QSystemTrayIcon::ActivationReason reason);

const char *reasonName(QSystemTrayIcon::ActivationReason reason)
{
    // ActivationReason has no Q_ENUM, so QMetaEnum cannot name it for us.
    switch (reason) {
    case QSystemTrayIcon::Trigger: return "Trigger (left)";
    case QSystemTrayIcon::DoubleClick: return "DoubleClick (left x2)";
    case QSystemTrayIcon::MiddleClick: return "MiddleClick";
    case QSystemTrayIcon::Context: return "Context (right)";
    case QSystemTrayIcon::Unknown: return "Unknown";
    }
    return "?";
}

} // namespace

QtTray::QtTray(Settings *settings, IAutostart *autostart, QObject *parent)
    : ITray(parent)
    , m_settings(settings)
    , m_autostart(autostart)
{
    m_tray = new QSystemTrayIcon(createIcon(), this);
    m_toolTipSub = QStringLiteral("Press Ctrl+C twice, or double-click / "
                                 "middle-click here, to translate the clipboard");

    buildMenu();
    m_tray->setContextMenu(m_menu);
    refreshToolTip();

    connect(m_tray, &QSystemTrayIcon::activated, this, &QtTray::activationRequested);
    connect(m_settings, &Settings::changed, this, [this]() {
        syncLanguageChecks();
        syncAutostartCheck();
    });
}

QtTray::~QtTray()
{
    // The icon holds a pointer to the menu: destroy it first, or the
    // QSystemTrayIcon destructor dereferences freed memory (the same ordering
    // bug KdeTray had, found by tests/unit/tst_trayparity.cpp).
    probe("dtor enter");
    delete m_tray;
    m_tray = nullptr;
    probe("tray deleted");
    delete m_menu;
    probe("menu deleted");
    m_menu = nullptr;
}

bool QtTray::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void QtTray::show()
{
    m_tray->show();
}

void QtTray::setBusy(bool busy)
{
    m_busy = busy;
    refreshToolTip();
}

void QtTray::setToolTipSub(const QString &sub)
{
    m_toolTipSub = sub;
    refreshToolTip();
}

void QtTray::refreshToolTip()
{
    // "Traduku - <hint>" normally; the busy state has to live here because
    // QSystemTrayIcon, unlike KStatusNotifierItem, has no overlay icon.
    m_tray->setToolTip(m_busy ? QStringLiteral("Traduku - Translating…")
                              : QStringLiteral("Traduku - %1").arg(m_toolTipSub));
}


void QtTray::activationRequested(QSystemTrayIcon::ActivationReason reason)
{
    // Which button the host meant is not observable otherwise, and on GNOME
    // it is exactly the question the panel decides for us (Qt asks for
    // Activate: QStatusNotifierItemAdaptor::itemIsMenu() returns false).
    qInfo("traduku: tray activated: %s", reasonName(reason));

    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::MiddleClick:
        // KdeTray maps both activateRequested and secondaryActivateRequested
        // to "translate the clipboard".
        Q_EMIT translateRequested();
        break;
    case QSystemTrayIcon::DoubleClick:
        // Qt emits a Trigger first; translating twice would be two requests.
        break;
    case QSystemTrayIcon::Context:   // the menu, the host shows it
    case QSystemTrayIcon::Unknown:
        // The menu is the host's business, not ours.
        break;
    }
}

QIcon QtTray::createIcon()
{
    // 1. a real theme icon when the platform theme is reachable (it is in
    //    Plasma, and "accessories-dictionary" is what KdeTray asks for);
    for (const char *name : {"traduku", "accessories-dictionary",
                             "preferences-desktop-locale"}) {
        const QIcon icon = QIcon::fromTheme(QLatin1String(name));
        if (!icon.isNull()) {
            return icon;
        }
    }
    // 2. otherwise pixels we own. Measured on GNOME/Wayland: QIcon cannot
    //    resolve the desktop theme from this process (hasThemeIcon() was
    //    false), and Traduku is not installed, so no theme knows its name -
    //    a null QIcon makes the host paint its "..." placeholder.
    QIcon icon;
    for (int size : {22, 32, 44, 64}) {
        icon.addPixmap(QPixmap::fromImage(paintedIcon(size)));
    }
    return icon;
}

void QtTray::setStatusLabel(const QString &text)
{
    if (!m_statusAction) {
        // First row, disabled: a header, not something to click.
        m_statusAction = m_menu->addAction(text);
        m_statusAction->setEnabled(false);
        m_menu->insertSeparator(m_statusAction);   // row, then the real items
        return;
    }
    m_statusAction->setText(text);
}

void QtTray::buildMenu()
{
    // Unparented (QMenu wants a QWidget*, the tray is a QObject); freed in
    // ~QtTray after the icon, which keeps a pointer to it.
    m_menu = new QMenu;

    auto *translate = m_menu->addAction(QStringLiteral("Translate Clipboard Now"));
    connect(translate, &QAction::triggered, this, &QtTray::translateRequested);

    auto *langMenu = m_menu->addMenu(QStringLiteral("Translate To"));
    for (const auto &lang : languages()) {
        auto *act = langMenu->addAction(lang.name);
        act->setCheckable(true);
        act->setData(lang.code);   // so a menu entry can be found by language
        connect(act, &QAction::triggered, this, [this, code = lang.code]() {
            m_settings->setTargetLanguage(code);
            m_settings->save();
            Q_EMIT targetLanguagePicked(code);
            syncLanguageChecks();
        });
        m_languageActions.insert(lang.code, act);
    }

    m_menu->addSeparator();

    auto *settings = m_menu->addAction(QStringLiteral("Settings…"));
    connect(settings, &QAction::triggered, this, &QtTray::settingsRequested);

    m_autostartAction = m_menu->addAction(QStringLiteral("Launch on Login"));
    m_autostartAction->setCheckable(true);
    connect(m_autostartAction, &QAction::toggled, this, [this](bool on) {
        if (!m_autostart->setEnabled(on)) {
            // Do not leave a tick on a backend that refused.
            const QSignalBlocker blocker(m_autostartAction);
            m_autostartAction->setChecked(!on);
            return;
        }
        syncAutostartCheck();
    });
    syncAutostartCheck();

    auto *quit = m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                   QStringLiteral("Quit"));
    // An intent, not QApplication::quit(): the tray stays a collector of
    // intents and the shell decides what leaving means (unsaved dialog etc).
    connect(quit, &QAction::triggered, this, &QtTray::quitRequested);

    // Discoverability, earned by measurement. Ubuntu's appindicator host
    // ignores the SNI ToolTip property entirely, and there a single left click
    // - what everyone tries first - opens the menu instead of translating
    // (indicatorStatusIcon.js: single/right -> menu.toggle(), Activate comes
    // only from a double click or the middle button). In Plasma a plain left
    // click does activate, so the wording covers both hosts instead of
    // describing one of them wrongly.
    auto *hint = m_menu->addAction(
        QStringLiteral("Click to translate (double-click if this menu opens instead)"));
    hint->setEnabled(false);

    syncLanguageChecks();
}

void QtTray::syncLanguageChecks()
{
    const QString cur = m_settings->targetLanguage();
    for (auto it = m_languageActions.cbegin(); it != m_languageActions.cend(); ++it) {
        it.value()->setChecked(it.key() == cur);
    }
}

void QtTray::syncAutostartCheck()
{
    m_autostartAction->setChecked(m_autostart->isEnabled());
}

} // namespace lt
