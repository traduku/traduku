/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinMediaPlayer.h"

#include "core/settings.h"

#include <QDebug>

#include <QAudioFormat>
#include <QMediaPlayer>
#include <QPointer>
#include <QUrl>
#include <QVector>

namespace lt {

namespace {

QPointer<QMediaPlayer> &sharedPlayer()
{
    // The cues are short and sequential; one session, reused, is what keeps
    // "started" from still playing when "finished" arrives. A parentless
    // QMediaPlayer is fine: it is owned here for the life of the app, and Qt
    // keeps it alive while it has a source.
    static QPointer<QMediaPlayer> player;
    return player;
}

} // namespace

SoundPlayer::Player windowsMediaPlayerPlayer()
{
    return [](const QString &file) {
        if (windowsWmfPluginPath().isEmpty()) {
            return false;      // no WMF: let the chain fall through to powershell
        }
        QPointer<QMediaPlayer> &player = sharedPlayer();
        if (!player) {
            player = new QMediaPlayer(nullptr);
        }
        player->stop();
        player->setSource(QUrl::fromLocalFile(file));
        player->play();
        // error() is synchronous for the failures we can act on (file gone,
        // container unplayable); anything later is logged by Qt and is the
        // media stack's business, not a reason to spawn a shell per cue.
        if (player->error() != QMediaPlayer::NoError) {
            qWarning("traduku: the media player refused %s: %s",
                     qUtf8Printable(file), qUtf8Printable(player->errorString()));
            return false;
        }
        return true;
    };
}

QVector<SoundPlayer::Player> windowsPlayerChain()
{
    return {windowsMediaPlayerPlayer(), windowsPowerShellPlayer()};
}

SoundPlayer *createWindowsSoundPlayer(Settings *settings, QObject *parent)
{
    SoundPlayer::Profile profile = windowsSoundProfile();
    profile.chain = &windowsPlayerChain;
    return new SoundPlayer(settings, {}, {windowsMediaDir()},
                           /*includeSystemThemeDirs=*/false,
                           windowsSoundFinder(), parent, std::move(profile));
}

} // namespace lt
