/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include "core/soundplayer.h"

#include <QString>

namespace lt {

/*!
 * The Windows sound profile: which files in %WINDIR%\Media stand for start,
 * finish and failure, and what to say when either the files or a player are
 * missing (ROADMAP.md §7.2).
 *
 * It is a profile rather than an `#ifdef` in core for the reason every other
 * injected platform fact in this codebase is: the alternative is two copies of
 * "how sound works" that drift, plus a core that knows which OS it is on,
 * which W5 forbids. It also means the profile is unit-testable on a Linux CI
 * box - the assertions below are about the *words*, and wrong words are the
 * part that hurts a user.
 *
 * Multiple candidate names per event, first hit wins: Windows does not ship
 * one fixed set of sounds (skins, N editions, a user who silenced the theme),
 * and "no sound at all because one filename is absent" is not a failure mode
 * worth shipping.
 */
SoundPlayer::Profile windowsSoundProfile();

/// %WINDIR%\Media, or "" when the environment has no WINDIR to work from.
QString windowsMediaDir();

/// The candidate names for one event, in the order they are tried.
QStringList windowsSoundCandidates(SoundEvent event);

/// Where Qt Multimedia's WMF backend lives, "" when it is not installed.
/// A stripped Windows (N edition without the media feature pack) has no WMF
/// plugin, and that is exactly the case a fallback exists for - so it has to be
/// asked, not assumed.
QString windowsWmfPluginPath();

/*!
 * The ToolFinder for the Windows profile.
 *
 * `qmediaplayer` is answered with the WMF plugin file, not with a program: the
 * file is what makes "this player is available" true, and Status::player has to
 * name *something* the user can check. The chain that actually plays is
 * supplied by the profile (SoundPlayer::Profile::chain), so nothing ever tries
 * to execute the path this returns.
 */
SoundPlayer::ToolFinder windowsSoundFinder();

/// The fallback player: a detached powershell running Media.SoundPlayer.
/// Slow by a process start, and the only thing left when WMF is absent.
SoundPlayer::Player windowsPowerShellPlayer();

} // namespace lt
