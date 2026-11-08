/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

#include "WinSound.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace lt {

namespace {

// The names are the ones a stock Windows 10/11 keeps its system sounds under,
// chosen for what they sound like rather than for what the theme calls them:
// a short tick for "started", a two-note chime for "ready", and the error
// sound the rest of the OS uses for "that did not work". Each event gets
// several because the folder is skinable, and an install that lacks one name
// must not be a silent install.
const char *const kStarted[] = {
    "Windows Menu Command.wav", "Windows Feed Discovered.wav", "Windows Ding.wav"};
const char *const kFinished[] = {
    "Windows Notify System Generic.wav", "Windows Print complete.wav",
    "Windows Ding.wav"};
const char *const kFailed[] = {
    "Windows Error.wav", "Windows Critical Stop.wav", "Windows Hardware Fail.wav"};

QStringList namesOf(const char *const *names, int count)
{
    QStringList out;
    for (int i = 0; i < count; ++i) {
        out << QString::fromUtf8(names[i]);
    }
    return out;
}

QStringList candidatesFor(SoundEvent event)
{
    switch (event) {
    case SoundEvent::Started:  return namesOf(kStarted, 3);
    case SoundEvent::Finished: return namesOf(kFinished, 3);
    case SoundEvent::Failed:   return namesOf(kFailed, 3);
    }
    return {};
}

} // namespace

QString windowsMediaDir()
{
    const QString windir = qEnvironmentVariable("WINDIR");
    if (windir.isEmpty()) {
        return {};
    }
    return QDir(windir).filePath(QStringLiteral("Media"));
}

QStringList windowsSoundCandidates(SoundEvent event)
{
    return candidatesFor(event);
}

QString windowsWmfPluginPath()
{
    // Asked, never assumed: an N edition without the media feature pack has no
    // WMF plugin, and that is precisely the case the fallback chain exists for.
    for (const QString &base : QCoreApplication::libraryPaths()) {
        const QString candidate = QDir(base).filePath(
            QStringLiteral("multimedia/windowsmediaplugin.dll"));
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

SoundPlayer::ToolFinder windowsSoundFinder()
{
    return [](const QString &program) {
        if (program == QLatin1String("qmediaplayer")) {
            return windowsWmfPluginPath();
        }
        return QStandardPaths::findExecutable(program);
    };
}

SoundPlayer::Player windowsPowerShellPlayer()
{
    return [](const QString &file) {
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("powershell"));
        if (exe.isEmpty()) {
            return false;
        }
        // Single quotes are escaped by doubling them, the way PowerShell wants
        // it: a sound file called C:\Sounds\it's.wav is a filename, not an
        // argument-injection vector.
        QString escaped = file;
        escaped.replace(QChar(0x0027), QStringLiteral("''"));
        const QString script =
            QStringLiteral("(New-Object Media.SoundPlayer '%1').PlaySync()").arg(escaped);
        return QProcess::startDetached(exe, {QStringLiteral("-NoProfile"),
                                             QStringLiteral("-NonInteractive"),
                                             QStringLiteral("-WindowStyle"),
                                             QStringLiteral("Hidden"),
                                             QStringLiteral("-Command"), script});
    };
}

SoundPlayer::Profile windowsSoundProfile()
{
    SoundPlayer::Profile p;
    p.name = QStringLiteral("windows");
    p.fileNames = [](SoundEvent event) { return candidatesFor(event); };
    // Two ways to make a sound, in the order a user would prefer them:
    //  - qmediaplayer: in-process, through Qt Multimedia's WMF backend. The
    //    "program" is not a program - the finder answers with the WMF plugin
    //    file, because that file is what makes the answer true (see
    //    windowsSoundFinder()).
    //  - powershell: Media.SoundPlayer, a detached process. Slower by a
    //    process start, and it exists because WMF is the one component a
    //    stripped Windows install can be missing.
    p.players = {QStringLiteral("qmediaplayer"), QStringLiteral("powershell")};
    p.missingThemeAdvice = QStringLiteral(
        "no system sounds: %1 was not found in %2 (Windows keeps them under "
        "%WINDIR%\\Media - restore that folder, or point [Sounds] ThemeDir at a "
        "directory of .wav files)");
    p.missingPlayerAdvice = QStringLiteral(
        "nothing can play %1: neither the built-in media player (WMF) nor any of "
        "%2 is available in this Windows installation");
    return p;
}

} // namespace lt
