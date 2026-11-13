/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinMediaPlayer.h"

#include "core/settings.h"

#include <QDebug>

#include <QAudioFormat>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
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

/// The player of the in-process backend, created with the one thing Qt 6
/// refuses to create by itself.
///
/// Qt 5's QMediaPlayer owned an implicit audio output; Qt 6 removed it, and a
/// player with no QAudioOutput set is the quietest failure mode in the port:
/// it opens the file, decodes it, sits in PlayingState, answers NoError, and
/// puts nothing on the speaker. Every measurement the product makes of itself -
/// the bool this chain returns, the file --backend-info prints, the unit tests
/// that inject a directory - reads "success". The only witness is a person
/// sitting at the laptop, and one said "the start cue is silent" (ROADMAP.md
/// §7.8 item 2). So the output is created here, where the player is, and the
/// player answers false when it cannot be created: "no cue is inaudible" is a
/// property of this function, not of the theme.
QMediaPlayer *playerWithAudioOutput()
{
    QPointer<QMediaPlayer> &player = sharedPlayer();
    if (!player) {
        player = new QMediaPlayer(nullptr);
    }
    if (!player->audioOutput()) {
        auto *out = new QAudioOutput(player);
        player->setAudioOutput(out);   // takes ownership
        if (out->device().isNull()) {
            // No output device: the machine has no speaker, or the audio
            // service is not answering. Say so by returning false - the chain
            // still tries the shell player, and if that is mute too the cue is
            // reported as unplayable rather than played to nobody.
            qWarning("traduku: no audio output device, so the media player "
                     "cannot make the cue audible");
            return nullptr;
        }
    }
    return player.data();
}

} // namespace

QMediaPlayer *windowsSharedMediaPlayer()
{
    return sharedPlayer().data();
}

SoundPlayer::Player windowsMediaPlayerPlayer()
{
    return [](const QString &file) {
        if (windowsWmfPluginPath().isEmpty()) {
            return false;      // no WMF: let the chain fall through to powershell
        }
        QMediaPlayer *player = playerWithAudioOutput();
        if (!player) {
            return false;      // nothing to play to: see the warning above
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
    // The device is opened here, at the moment the shell builds its sound, and
    // not at the first cue. The start cue is the shortest sound the product
    // makes and the first one a session plays, so on a cold player it is also
    // the only one short enough to be lost to the open: the probe of 2026-11-08
    // found the player still buffering 250 ms after play() on a cue that is
    // 150 ms long. A tray app is alive for hours before anyone presses Ctrl+C
    // twice; paying the open here is free, paying it mid-gesture is the bug.
    auto *sound = new SoundPlayer(settings, {}, {windowsMediaDir()},
                                  /*includeSystemThemeDirs=*/false,
                                  windowsSoundFinder(), parent, std::move(profile));
    playerWithAudioOutput();
    return sound;
}

} // namespace lt
