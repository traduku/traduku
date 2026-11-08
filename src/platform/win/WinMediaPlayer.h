/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include "WinSound.h"

class QObject;

namespace lt { class Settings; class SoundPlayer; }

namespace lt {

/*!
 * The in-process player: Qt Multimedia's WMF backend, compiled on Windows
 * only, because it is the one place the port links Qt6::Multimedia.
 *
 * One shared QMediaPlayer is deliberate. A fresh one per cue is the obvious
 * implementation and the wrong one: WMF takes a moment to open its session, so
 * the start chime would still be starting when the finish chime arrives, and
 * the two would cut each other off. Reusing the instance keeps the cues in
 * order, which is the entire point of having three of them.
 *
 * It reports "played" when playback started without an immediate error; a
 * codec that fails later logs and is the user's Windows to fix, not a reason
 * to spawn a shell.
 */
SoundPlayer::Player windowsMediaPlayerPlayer();

/// WMF first, powershell second - the order a user would prefer, and the one
/// that keeps working on a Windows without the media feature pack.
QVector<SoundPlayer::Player> windowsPlayerChain();

/*!
 * The SoundPlayer the Windows shell uses: the Windows profile, the player
 * chain wired into it, the %WINDIR%\Media directory and the finder that can
 * say which player is actually present.
 *
 * One function rather than "build a SoundPlayer and remember to set the
 * chain": a profile whose chain nobody installs would try to *execute* the WMF
 * plugin path, and that is a failure a code review would not catch and a user
 * would experience as "sound does nothing".
 */
SoundPlayer *createWindowsSoundPlayer(Settings *settings, QObject *parent = nullptr);

} // namespace lt
