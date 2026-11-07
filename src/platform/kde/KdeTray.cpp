#include "KdeTray.h"

#include "core/languages.h"
#include "core/settings.h"
#include "interfaces/iautostart.h"

#include <KStatusNotifierItem>

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>

namespace lt {

KdeTray::KdeTray(Settings *settings, IAutostart *autostart, QObject *parent)
    : ITray(parent)
    , m_settings(settings)
    , m_autostart(autostart)
{
    m_tray = new KStatusNotifierItem(QStringLiteral("traduku"), this);
    m_tray->setTitle(QStringLiteral("Traduku"));
    m_tray->setIconByName(QStringLiteral("accessories-dictionary"));
    m_tray->setToolTipIconByName(QStringLiteral("accessories-dictionary"));
    m_tray->setToolTipTitle(QStringLiteral("Traduku"));
    m_tray->setToolTipSubTitle(QStringLiteral("Press Ctrl+C twice to translate "
                                              "the clipboard"));
    m_tray->setStandardActionsEnabled(false);
    m_tray->setStatus(KStatusNotifierItem::Passive);

    connect(m_tray, &KStatusNotifierItem::activateRequested, this,
            [this](bool active, const QPoint &) {
                if (active) {
                    Q_EMIT translateRequested();
                }
            });
    connect(m_tray, &KStatusNotifierItem::secondaryActivateRequested, this,
            [this](const QPoint &) { Q_EMIT translateRequested(); });

    buildMenu();
    m_tray->setContextMenu(m_menu);

    connect(m_settings, &Settings::changed, this, [this]() {
        syncLanguageChecks();
    });
}

KdeTray::~KdeTray()
{
    // Measured on Plasma (KF6 6.31, Qt 6.12): destroying a KStatusNotifierItem
    // with a session bus present segfaults in BOTH orders - `delete menu;
    // delete tray` and `delete tray; delete menu` - while deleteLater() is
    // safe. So nothing here is deleted synchronously.
    //
    // This is not theoretical: kde/main.cpp used to hold the tray on the
    // stack, so quitting the KDE app ran this destructor, i.e. the app crashed
    // on exit. Found by tests/unit/tst_trayparity.cpp, which destroys trays
    // the way the shell does. The shell now also keeps the tray alive for the
    // lifetime of the process; this destructor is what a library user gets.
    if (m_tray) {
        m_tray->deleteLater();
        m_tray = nullptr;
    }
    if (m_menu) {
        m_menu->deleteLater();
        m_menu = nullptr;
    }
}

QMenu *KdeTray::menu() const
{
    return m_menu;
}

QString KdeTray::backendName() const
{
    return QStringLiteral("kde (KStatusNotifierItem)");
}

void KdeTray::show()
{
    // No-op on purpose: KStatusNotifierItem registers with the watcher in its
    // constructor and has no show()/hide() (it is a QObject, not a widget).
    // The call exists so the shell drives both trays through ITray.
}

bool KdeTray::isAvailable() const
{
    // Plasma always owns the StatusNotifierWatcher; if it ever does not,
    // KStatusNotifierItem simply stays unregistered - still not fatal.
    return true;
}

void KdeTray::setBusy(bool busy)
{
    m_tray->setOverlayIconByName(busy ? QStringLiteral("emblem-added")
                                      : QString());
}

void KdeTray::setToolTipSub(const QString &sub)
{
    m_tray->setToolTipSubTitle(sub);
}

void KdeTray::buildMenu()
{
    m_menu = new QMenu;

    auto *translate = m_menu->addAction(QStringLiteral("Translate Clipboard Now"));
    connect(translate, &QAction::triggered, this, &KdeTray::translateRequested);

    auto *langMenu = m_menu->addMenu(QStringLiteral("Translate To"));
    for (const auto &lang : languages()) {
        auto *act = langMenu->addAction(lang.name);
        act->setCheckable(true);
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
    connect(settings, &QAction::triggered, this, &KdeTray::settingsRequested);

    m_autostartAction = m_menu->addAction(QStringLiteral("Launch on Login"));
    m_autostartAction->setCheckable(true);
    connect(m_autostartAction, &QAction::toggled, this, [this](bool on) {
        m_autostart->setEnabled(on);
        syncAutostartCheck();
    });
    syncAutostartCheck();

    auto *quit = m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                   QStringLiteral("Quit"));
    // An intent, like QtTray's: the shell decides what leaving means. (It
    // used to call QApplication::quit() here directly, which made the two
    // trays impossible to test the same way.)
    connect(quit, &QAction::triggered, this, &KdeTray::quitRequested);

    syncLanguageChecks();
}

void KdeTray::syncLanguageChecks()
{
    const QString cur = m_settings->targetLanguage();
    for (auto it = m_languageActions.cbegin(); it != m_languageActions.cend(); ++it) {
        it.value()->setChecked(it.key() == cur);
    }
}

void KdeTray::syncAutostartCheck()
{
    m_autostartAction->setChecked(m_autostart->isEnabled());
}

} // namespace lt
