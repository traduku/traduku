/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

// The Unix/Linux half of app/PlatformBackends.h: the clipboard decision lives
// in platform/wl (wl-clipboard -> X11 -> Qt, per session), autostart is the
// XDG entry, and the sounds come from the freedesktop theme.

#include "app/PlatformBackends.h"
#include "app/SingleInstance.h"

#include "core/settings.h"
#include "core/soundplayer.h"
#include "platform/qt/SessionInfo.h"
#include "platform/qt/XdgAutostart.h"
#include "platform/wl/ClipboardFactory.h"
#include "platform/wl/WlClipboard.h"

#include <QByteArray>
#include <QClipboard>
#include <QLocalServer>
#include <QStringList>
#include <QTemporaryDir>

namespace lt {

namespace {

const char *const kSelfTestId = "traduku-selftest";

} // namespace

std::unique_ptr<IClipboard> createDefaultClipboard(QClipboard *qtClipboard)
{
    return createClipboard(sessionType(), {}, qtClipboard);
}

std::unique_ptr<IAutostart> createDefaultAutostart(const QString &exePath)
{
    // The id IS the file name, and it has to be the name the package uses
    // (/etc/xdg/autostart/traduku.desktop): XDG shadowing works by file name.
    return std::make_unique<XdgAutostart>(QStringLiteral("traduku"), exePath);
}

SoundPlayer *createDefaultSoundPlayer(Settings *settings, QObject *parent)
{
    return new SoundPlayer(settings, {}, {}, /*includeSystemThemeDirs=*/true,
                           {}, parent);
}

SelfTestRow probePlatformClipboard()
{
    // The reader is chosen by whether the tools exist, so the row asks that
    // question in both directions. Checking only "usable when the tools are
    // there" is green for a reader that claims to work always - and the Wayland
    // bug this guards is exactly a reader that said yes when it could not see
    // the clipboard.
    SelfTestRow r;
    r.what = QStringLiteral("clipboard: wl-clipboard is chosen only when "
                            "wl-paste and wl-copy are on PATH");
    const auto withTools = [](const QString &tool) {
        return QStringList{QStringLiteral("wl-paste"), QStringLiteral("wl-copy")}
                       .contains(tool)
            ? QStringLiteral("/usr/bin/") + tool
            : QString();
    };
    const auto withoutTools = [](const QString &) { return QString(); };
    r.ok = WlClipboard::isUsable({}, withTools)
        && !WlClipboard::isUsable({}, withoutTools);
    return r;
}

SelfTestRow probePlatformAutostart(const QString &exePath)
{
    SelfTestRow r;
    r.what = QStringLiteral("autostart: xdg desktop entry write/remove");
    QTemporaryDir dir;
    if (!dir.isValid()) {
        r.ok = false;
        return r;
    }
    // Isolated config home: the selftest must not decide for the user whether
    // their app starts tomorrow.
    const QByteArray saved = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", (dir.path() + QStringLiteral("/config")).toUtf8());
    XdgAutostart a(QString::fromLatin1(kSelfTestId), exePath);
    r.ok = !a.isEnabled() && a.setEnabled(true) && a.isEnabled()
        && a.setEnabled(false) && !a.isEnabled();
    if (!saved.isEmpty()) {
        qputenv("XDG_CONFIG_HOME", saved);
    } else {
        qunsetenv("XDG_CONFIG_HOME");
    }
    return r;
}

namespace {

/// The Unix claim is a bound socket path: bind() on an existing path fails, and
/// the kernel clears it when the owner dies. It is a *separate* name from the
/// activation channel, because SingleInstance removes a stale channel file
/// before listening, and a token that can be removed by its own claimant is
/// not a token.
class SocketToken final : public IInstanceToken
{
public:
    explicit SocketToken(QString name)
        : m_name(std::move(name))
        , m_server(new QLocalServer)
    {
    }

    ~SocketToken() override { delete m_server; }

    bool acquire() override
    {
        if (m_taken) {
            return true;
        }
        if (m_server->listen(m_name)) {
            m_taken = true;
            return true;
        }
        m_error = QStringLiteral("token %1 is already claimed: %2")
                      .arg(m_name, m_server->errorString());
        return false;
    }

    void release() override
    {
        if (m_taken) {
            m_server->close();
            QLocalServer::removeServer(m_name);
            m_taken = false;
        }
    }

    QString errorText() const override { return m_error; }
    QString description() const override { return m_name; }

private:
    QString m_name;
    QLocalServer *m_server;
    bool m_taken = false;
    QString m_error;
};

} // namespace

InstanceTokenFactory defaultManagerTokenFactory()
{
    return [](const QString &name) -> std::unique_ptr<IInstanceToken> {
        return std::make_unique<SocketToken>(name + QStringLiteral(".token"));
    };
}

} // namespace lt
