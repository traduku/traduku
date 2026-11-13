/*
 * This file is part of Traduku.
 * Copyright (C) 2026 Kirill Cherkashin
 * Licensed under the GNU General Public License v3.0
 */

// The Windows half of app/PlatformBackends.h: the clipboard is Qt's own
// (win32), autostart is a Run value, and the cues are the system .wav files.

#include "app/PlatformBackends.h"
#include "app/SingleInstance.h"

#include "platform/win/WinClipboardPolicy.h"

#include "core/settings.h"
#include "core/soundplayer.h"
#include "platform/qt/QtClipboard.h"
#include "platform/win/WinAutostart.h"
#include "platform/win/WinInstanceToken.h"
#include "platform/win/WinMediaPlayer.h"
#include "platform/win/WinRegistryStore.h"
#include "platform/win/WinSessionInfo.h"

#include <QApplication>
#include <QClipboard>
#include <QUuid>

namespace lt {

std::unique_ptr<IClipboard> createDefaultClipboard(QClipboard *qtClipboard)
{
    // Qt's clipboard on Windows is the platform's own: it is a real client of
    // the win32 clipboard, not the blind fallback it is on Wayland, so it
    // carries no diagnostic. The refusal above is what makes that visible
    // rather than assumed.
    return std::make_unique<QtClipboard>(qtClipboard);
}

std::unique_ptr<IAutostart> createDefaultAutostart(const QString &exePath)
{
    // The machine-wide key is passed as a foreign location: it starts the app
    // too, and an off-switch that cannot see it reports "off" while the app
    // keeps coming back (the XdgAutostart lesson, in a different hive).
    return std::make_unique<WinAutostart>(exePath, autostartRunKey(),
                                         QList<RunKey>{machineRunKey()},
                                         registryStoreFactory());
}

SoundPlayer *createDefaultSoundPlayer(Settings *settings, QObject *parent)
{
    return createWindowsSoundPlayer(settings, parent);
}

SelfTestRow probePlatformClipboard()
{
    SelfTestRow r;
    r.what = QStringLiteral("clipboard: the system clipboard round-trips text");

    QClipboard *clip = QApplication::clipboard();
    if (!clip) {
        r.skipReason = QStringLiteral("no clipboard in this session (no window "
                                      "station): the row needs the desktop");
        return r;
    }
    // Restored afterwards: --selftest is a check, not a user's copy. It still
    // takes clipboard ownership for a moment, which is the one side effect a
    // selftest cannot avoid while testing the thing that *is* ownership.
    const QString saved = clip->text();
    const QString probe = QStringLiteral("traduku-selftest-%1")
                              .arg(QCoreApplication::applicationPid());
    // setText() returns nothing on Qt (a clipboard does not refuse), so the
    // row is graded on what comes back out of it - which is the only answer
    // the user cares about either.
    clip->setText(probe);
    const bool readBack = clip->text() == probe;
    clip->setText(saved);
    r.ok = readBack && clip->text() == saved;
    return r;
}

SelfTestRow probePlatformAutostart(const QString &exePath)
{
    SelfTestRow r;
    r.what = QStringLiteral("autostart: the Run value is written, read and removed");
    // A scratch key of the same shape as the real one: same class, same store,
    // same code path - and the user's login sequence is never the test bench.
    const RunKey probe = selfTestRunKey(QStringLiteral("traduku"),
                                        QStringLiteral("selftest-%1")
                                            .arg(QCoreApplication::applicationPid()));
    WinAutostart a(exePath, probe, {}, registryStoreFactory());
    r.ok = !a.isEnabled() && a.setEnabled(true) && a.isEnabled()
        && a.setEnabled(false) && !a.isEnabled();
    // The row above removed the value; nothing else is left behind but the
    // scratch key itself, which is evidence that the check ran rather than
    // damage: it lives under Software\Traduku\SelfTest and starts nothing.
    return r;
}

InstanceTokenFactory defaultManagerTokenFactory()
{
    // The mutex, not the pipe: QLocalServer on Windows accepts a second
    // listener on the same name (measured in
    // tst_singleinstance::theDefaultTokenIsExclusive), so the claim has to come
    // from a kernel object that really is exclusive. See
    // platform/win/WinInstanceToken.h.
    return [](const QString &name) -> std::unique_ptr<IInstanceToken> {
        return std::make_unique<win::InstanceToken>(name);
    };
}

bool platformHasInteractiveDesktop()
{
    // The window station and the desktop object, which is what "is there a
    // screen here" means on this OS - not DISPLAY, which Windows does not set
    // and which once made every Windows desktop answer "no". See
    // PlatformBackends.h and tst_wingui.
    return queryWinSessionFacts().interactiveDesktop;
}

} // namespace lt
