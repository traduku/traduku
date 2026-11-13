// The Windows sound profile: which .wav files, which player, and - the part
// that is easy to get wrong and hard to notice - what the app tells a user
// when either is missing. A hint that says "sudo apt install wireplumber" on a
// machine with no apt is not a hint; it is a reason to stop reading hints.

#include <QtTest>

#include "platform/win/WinSound.h"
#include "core/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <memory>

#ifdef Q_OS_WIN
// The rows at the end of this file ask the machine it runs on, and the media
// player exists only here. WinMediaPlayer.cpp is the one file in the port that
// links Qt Multimedia, so this include is the one place a sound test may ask
// what the player is wired to.
#include "platform/win/WinMediaPlayer.h"
#include "core/settings.h"
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMediaPlayer>
#endif

using namespace lt;

namespace {

/// A directory that holds exactly the files this machine supposedly has.
/// SoundPlayer must not be asked about the machine it runs on (ROADMAP.md §2,
/// the ToolFinder rule); the test writes the machine.
QString mediaDirWith(QTemporaryDir &dir, const QStringList &names)
{
    for (const QString &n : names) {
        QFile f(QDir(dir.path()).filePath(n));
        if (f.open(QIODevice::WriteOnly)) {
            f.write("RIFF");           // existence is all resolve() checks
            f.close();
        }
    }
    return dir.path();
}

} // namespace

class TestWinSound : public QObject
{
    Q_OBJECT

private slots:
    void profileAsksForWavFilesOnly()
    {
        const SoundPlayer::Profile p = windowsSoundProfile();
        QCOMPARE(p.name, QStringLiteral("windows"));
        for (SoundEvent e : {SoundEvent::Started, SoundEvent::Finished,
                             SoundEvent::Failed}) {
            const QStringList names = p.fileNames(e);
            QVERIFY2(!names.isEmpty(), "a sound event with no candidate file");
            for (const QString &n : names) {
                QVERIFY2(n.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive),
                         qPrintable(QStringLiteral("Windows cannot play ") + n));
                // .oga is the freedesktop spelling; if it leaked in, the
                // player chain would find a file it cannot decode and say
                // nothing.
                QVERIFY(!n.endsWith(QStringLiteral(".oga")));
            }
        }
    }

    void adviceNeverNamesAPackageWindowsCannotInstall()
    {
        // The whole reason the advice strings moved into the profile.
        const SoundPlayer::Profile p = windowsSoundProfile();
        const QString advice = p.missingThemeAdvice + QLatin1Char(' ')
                             + p.missingPlayerAdvice;
        for (const QString &wrong : {QStringLiteral("apt"), QStringLiteral("pacman"),
                                     QStringLiteral("pw-play"), QStringLiteral("paplay"),
                                     QStringLiteral("wireplumber"),
                                     QStringLiteral("freedesktop-sound-theme")}) {
            QVERIFY2(!advice.contains(wrong, Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("Linux advice on Windows: ") + wrong));
        }
        QVERIFY2(!p.players.isEmpty(), "a profile with no player can never play");
    }

    void resolvesTheFirstCandidateThatExists()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SoundPlayer::Profile p = windowsSoundProfile();
        const QStringList names = p.fileNames(SoundEvent::Failed);
        QVERIFY(!names.isEmpty());
        // Only the LAST candidate exists: first-hit-wins over a list, not
        // "the name we like most or nothing".
        const QString media = mediaDirWith(dir, {names.last()});
        Settings s(dir.path() + QStringLiteral("/config.ini"));
        SoundPlayer player(&s, {}, {media}, false, {}, nullptr, p);
        QCOMPARE(player.resolve(SoundEvent::Failed),
                 QDir(media).filePath(names.last()));
    }

    void missingFileAdvisesAboutWindowsMediaNotAboutApt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SoundPlayer::Profile p = windowsSoundProfile();
        Settings s(dir.path() + QStringLiteral("/config.ini"));
        // Empty directory: no candidate file anywhere. Every player is
        // "installed" so the only complaint left is the missing file.
        SoundPlayer player(&s, {}, {dir.path()}, false,
                           [](const QString &prog) { return prog; }, nullptr, p);
        const auto st = player.status(SoundEvent::Finished);
        QVERIFY(st.file.isEmpty());
        QVERIFY2(!st.problem.isEmpty(), "mute, and nothing to read about it");
        // The remedy has to name the folder this OS keeps its sounds in.
        QVERIFY2(st.problem.contains(QStringLiteral("Media")),
                 qPrintable(st.problem));
    }

    void noPlayerIsReportedAsNoPlayerWithTheProfilePlayersNamed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const SoundPlayer::Profile p = windowsSoundProfile();
        const QString media = mediaDirWith(dir, p.fileNames(SoundEvent::Finished));
        Settings s(dir.path() + QStringLiteral("/config.ini"));
        // finder returns "" for everything: nothing installed.
        SoundPlayer player(&s, {}, {media}, false,
                           [](const QString &) { return QString(); }, nullptr, p);
        const auto st = player.status(SoundEvent::Finished);
        QVERIFY2(!st.file.isEmpty(), "the test directory lost its files");
        QVERIFY(st.player.isEmpty());
        QVERIFY2(!st.problem.isEmpty(), "a mute machine with nothing to say");
        for (const QString &prog : p.players) {
            if (st.problem.contains(prog)) {
                return;               // the advice named at least one candidate
            }
        }
        QFAIL(qPrintable(QStringLiteral("the advice names none of the profile's "
                                        "players: ") + st.problem));
    }

    // ---- the rows that ask THIS machine --------------------------------
    //
    // Everything above writes its own machine: a temporary directory holding
    // the files a machine *could* have. Such a row stays green on a machine
    // whose %WINDIR%\Media is empty - and an empty sound folder is precisely
    // the machine of the user who reports "there is no sound". The hand walk
    // of ROADMAP.md §7.8 found the start cue silent with every one of the rows
    // above green, so a row that asks the real folder is not a duplicate of
    // them, it is the one they cannot be.
    void everyCueOfTheProfileResolvesOnThisMachine()
    {
        const QString media = windowsMediaDir();
        if (media.isEmpty() || !QFileInfo::exists(media)) {
            QSKIP("no %WINDIR%\\Media on this machine - there is no theme to resolve");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(dir.path() + QStringLiteral("/config.ini"));
        SoundPlayer player(&s, {}, {media}, false,
                           [](const QString &prog) { return prog; },
                           nullptr, windowsSoundProfile());
        for (SoundEvent e : {SoundEvent::Started, SoundEvent::Finished,
                             SoundEvent::Failed}) {
            const QString file = player.resolve(e);
            QVERIFY2(!file.isEmpty(),
                     qPrintable(QStringLiteral("no candidate of this profile exists in ")
                                + media));
            QVERIFY2(QFile::exists(file), qPrintable(file));
            // "Resolves" is not the same as "the report is clean": status()
            // is the sentence --backend-info prints, and a cue that resolves
            // must not come back with a problem attached to it.
            const auto st = player.status(e);
            QVERIFY2(st.problem.isEmpty(), qPrintable(st.problem));
        }
    }

#ifdef Q_OS_WIN
    /// A QMediaPlayer with no QAudioOutput is the most convincing silent bug
    /// in Qt 6: it loads the file, decodes it, sits in PlayingState, reports
    /// NoError - and puts nothing on the speaker, because Qt 6 removed the
    /// implicit audio output the player used to own.
    ///
    /// The product's own contract makes that observable: play() returns true
    /// only for a player that can reach a speaker, because the return value is
    /// what stops the chain from trying powershell. So the assertion is not
    /// "did a sound come out of the laptop" (only a person can answer that, and
    /// one did, in §7.8) - it is "the player that claims success is wired to an
    /// output at all".
    void theInProcessPlayerIsWiredToASpeaker()
    {
        const QString cue = firstCueOnThisMachine();
        if (cue.isEmpty()) {
            QSKIP("no %WINDIR%\\Media cue on this machine to play");
        }
        const bool played = windowsMediaPlayerPlayer()(cue);
        QMediaPlayer *player = windowsSharedMediaPlayer();
        QVERIFY2(player, "playing a cue did not create the player the app reuses");
        QVERIFY2(played,
                 qPrintable(QStringLiteral("the in-process player refused ") + cue
                            + QStringLiteral("; the chain would fall through to "
                                            "powershell, and the report below is "
                                            "about a player nobody uses")));
        QVERIFY2(player->audioOutput() != nullptr,
                 "QMediaPlayer played a cue and reported success with no "
                 "QAudioOutput attached: Qt 6 decodes and plays to nobody in "
                 "that state, which is the silent start cue of ROADMAP.md "
                 "\u00a77.8 item 2");
    }

    /// The failure half of the same contract: a machine with no speaker must
    /// hear "nothing can be played here", not silence dressed as success.
    void aCueWithNoSpeakerIsRefusedSoTheChainFallsThrough()
    {
        if (!QMediaDevices::audioOutputs().isEmpty()) {
            QSKIP("this machine has a speaker; the mute path cannot be measured here");
        }
        const QString cue = firstCueOnThisMachine();
        if (cue.isEmpty()) {
            QSKIP("no cue file to hand a player that has nowhere to play it");
        }
        QVERIFY(!windowsMediaPlayerPlayer()(cue));
    }
#endif

    void theFreedesktopDefaultIsStillTheDefault()
    {
        // The Windows profile must not become the profile: a Linux shell that
        // silently started asking for .wav files would sound different and
        // test different.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(dir.path() + QStringLiteral("/config.ini"));
        SoundPlayer player(&s, {}, {dir.path()}, false);
        QCOMPARE(player.profile().name, QStringLiteral("freedesktop"));
        mediaDirWith(dir, {SoundPlayer::fileName(SoundEvent::Finished)});
        SoundPlayer withFile(&s, {}, {dir.path()}, false);
        QVERIFY(withFile.resolve(SoundEvent::Finished).endsWith(
            QStringLiteral("complete.oga")));
    }

private:
#ifdef Q_OS_WIN
    /// The first candidate of the first cue that this machine really has. The
    /// rows above invent a media dir; these two ask for a file the OS ships,
    /// because "the start cue is silent" is a claim about one.
    QString firstCueOnThisMachine() const
    {
        const QString media = windowsMediaDir();
        if (media.isEmpty()) {
            return {};
        }
        for (SoundEvent e : {SoundEvent::Started, SoundEvent::Finished,
                             SoundEvent::Failed}) {
            for (const QString &name : windowsSoundCandidates(e)) {
                const QString path = QDir(media).filePath(name);
                if (QFile::exists(path)) {
                    return path;
                }
            }
        }
        return {};
    }
#endif
};

QTEST_GUILESS_MAIN(TestWinSound)
#include "tst_winsound.moc"
