/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#pragma once

#include <QString>
#include <memory>

class QClipboard;

namespace lt {

class IAutostart;
class IClipboard;
class Settings;
class SoundPlayer;

/*!
 * The two backends whose *identity* differs per OS, and nothing else.
 *
 * Everything else the shell assembles (the tray, the window, the notifier's
 * interface, the engine) is the same object on every platform; clipboard and
 * autostart are the two where the platform is not an implementation detail
 * but the thing itself - one reads a compositor's selection, the other a
 * registry value. They are chosen here, in exactly one file per OS, so
 * `--translate`, `--backend-info` and `--selftest` stay one code path on
 * every desktop. The rule that made this file necessary is the same one that
 * made app/cli.cpp necessary: a rule kept twice eventually disagrees with
 * itself, and the user is the one who finds out.
 */
std::unique_ptr<IClipboard> createDefaultClipboard(QClipboard *qtClipboard);
std::unique_ptr<IAutostart> createDefaultAutostart(const QString &exePath);

/// The cue player of this platform, already wired with the right file names,
/// the right players and - the part that is easy to get wrong - the advice a
/// user of this OS can actually act on.
SoundPlayer *createDefaultSoundPlayer(Settings *settings, QObject *parent = nullptr);

/// One row of `traduku --selftest`, asked of this platform.
///
/// The rows are per-OS because a check that tests something the product does
/// not do here is worse than a missing check: it is green, and it teaches the
/// reader that green means nothing. The Windows clipboard probe cannot be
/// "wl-paste is on PATH", because on Windows the answer would be yes-or-no
/// about a tool the app never calls.
struct SelfTestRow
{
    QString what;   ///< printed after PASS/FAIL, so it has to name the thing
    bool ok = false;
    /// A row that does not apply here, with the reason. Skipped is not
    /// silently deleted, and it is not green either (ROADMAP.md §4.5b).
    QString skipReason;
};

SelfTestRow probePlatformClipboard();
SelfTestRow probePlatformAutostart(const QString &exePath);

} // namespace lt
