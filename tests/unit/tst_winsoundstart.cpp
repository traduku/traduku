// The state of the sound path at the moment the Windows shell starts.
//
// This row is its own binary on purpose, and the reason is a measurement made
// while writing it. It first lived beside the other Windows sound rows, where
// it passed - and it passed for the wrong reason: the row above plays a cue,
// which creates the app's one shared QMediaPlayer, so by the time the startup
// row ran, "the player exists" was true for a reason that had nothing to do
// with startup. A row about what the app does when it starts is only honest in
// a process that is starting.

#include <QtTest>

#include "core/settings.h"
#include "platform/win/WinMediaPlayer.h"
#include "platform/win/WinSound.h"

#include <QDir>
#include <QFile>
#include <QMediaPlayer>
#include <QTemporaryDir>

#include <memory>

using namespace lt;

class TestWinSoundStart : public QObject
{
    Q_OBJECT

private slots:
    /// The first cue of a session is the shortest one the product has ("Windows
    /// Menu Command" is 150 ms - read off the file the same backend decodes)
    /// and it is the one that opens the audio device. The probe of 2026-11-08
    /// looked at the player 250 ms after play() and found it still buffering:
    /// by then the cue it was loading had a quarter of a second of audio in it,
    /// which is the whole cue. That is the second of the two reasons the start
    /// chime was silent (ROADMAP.md §7.8 item 2); the first is a player with no
    /// audio output at all, pinned in tst_winsound.
    ///
    /// The product's answer is to pay the cost where it is free: a tray app
    /// lives for hours between cues, so the device is opened when the shell
    /// builds its sound player, not when a person presses Ctrl+C twice and
    /// waits to hear the first 150 ms of a translation.
    void theSoundDeviceIsOpenBeforeTheFirstGesture()
    {
        const QString media = windowsMediaDir();
        if (media.isEmpty() || !QFileInfo::exists(media)) {
            QSKIP("no %WINDIR%\\Media on this machine - nothing to warm a player for");
        }
        bool hasCue = false;
        for (const QString &name : windowsSoundCandidates(SoundEvent::Started)) {
            if (QFile::exists(QDir(media).filePath(name))) {
                hasCue = true;
                break;
            }
        }
        if (!hasCue) {
            QSKIP("this Windows has none of the start-cue files");
        }

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(dir.path() + QStringLiteral("/config.ini"));

        QVERIFY2(windowsSharedMediaPlayer() == nullptr,
                 "the player existed before the shell was built, so this row is "
                 "measuring somebody else's startup");
        std::unique_ptr<SoundPlayer> sound(createWindowsSoundPlayer(&s));

        QMediaPlayer *player = windowsSharedMediaPlayer();
        QVERIFY2(player,
                 "building the shell's sound player did not create the player "
                 "its cues go through: the first gesture pays for opening the "
                 "audio device, and the start cue is 150 ms long");
        QVERIFY2(player->audioOutput(),
                 "the player exists at startup with no audio output attached");
    }
};

QTEST_GUILESS_MAIN(TestWinSoundStart)
#include "tst_winsoundstart.moc"
