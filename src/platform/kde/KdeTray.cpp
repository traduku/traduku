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
    : QObject(parent)
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
    delete m_menu;
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
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

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
