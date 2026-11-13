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
// and the ORDER is a measurement, not a preference: every file below was read
// off this machine's %WINDIR%\Media (RIFF/PCM16, 2026-11-09) for its length and
// its energy, and the row `theChosenCuesAreFilesAPersonCanHear` in tst_winsound
// enforces the floor those numbers produced.
//
// The list this replaces was written by reading folder names, and it produced
// the cue a person described as "a mosquito squeak": Windows Menu Command.wav,
// 0.11 s audible at a loudness of 0.0013, against a finish cue of 1.16 s at
// 0.1155 - a tenth of the length and a hundredth of the loudness.
//
// Loudness here is the rms of the loudest 100 ms, not of the whole file: the
// first version of the gate averaged the file, which scores a bell with a
// decay as if its decay were silence. On the user's own numbers it called the
// cue the ear chose "three times too quiet" (Windows Notify.wav: 0.0117
// averaged, 0.0262 at its loudest) while agreeing with the ear about the squeak
// (0.0011 averaged, 0.0013 at its loudest). The row
// `theLoudnessMetricIsAnchoredOnTheEarVerdicts` pins that ordering, so the
// floors cannot be tuned by taste.
//
// The head of kStarted is not a measurement: four candidates were played on a
// live desktop and the user chose Windows Notify.wav (2026-11-09). It is the
// quietest of the candidates that pass the floors, and the trade-off is taken
// knowingly: it is the same length as the finish cue and from the same family
// of chimes, and the two stay apart by loudness (0.0262 against 0.1155) and by
// the order they arrive in.
const char *const kStarted[] = {
    "Windows Notify.wav",           // 0.94 s audible, 0.0262 - chosen by ear
    "Windows Default.wav",          // 0.30 s, 0.0572
    "Windows Hardware Insert.wav",  // 0.46 s, 0.0618
    "Windows Message Nudge.wav",    // 1.17 s, 0.1131
    "Windows Background.wav",       // 1.15 s, 0.1647
    "Windows Ding.wav",             // 0.76 s, 0.0162 - below the floor, kept last
                                    // as the one name every Windows has
};
const char *const kFinished[] = {
    "Windows Notify System Generic.wav",  // 1.16 s audible, 0.1155 - the loudest of the family
    "Windows Notify Calendar.wav",        // 1.31 s, 0.1156
    "Windows Unlock.wav",                 // 1.82 s, 0.1005
    "Windows Print complete.wav",         // 1.45 s, 0.0269
};
const char *const kFailed[] = {
    "Windows Hardware Fail.wav",    // 0.49 s, 0.0585
    "Windows Exclamation.wav",      // 1.22 s, 0.0374
    "Windows Error.wav",            // 0.72 s, 0.0305
    "Windows Critical Stop.wav",    // 0.71 s, 0.0204
};
// Deliberately absent: Windows Balloon.wav (0.0370) is the sound Windows itself
// plays when OUR WinNotifier calls Shell_NotifyIcon. Using it as an app cue puts
// two copies of one sound in the same second, on the one platform where the
// notification channel has a voice of its own.

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
    case SoundEvent::Started:  return namesOf(kStarted, 6);
    case SoundEvent::Finished: return namesOf(kFinished, 4);
    case SoundEvent::Failed:   return namesOf(kFailed, 4);
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
