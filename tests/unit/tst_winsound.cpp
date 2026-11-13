// The Windows sound profile: which .wav files, which player, and - the part
// that is easy to get wrong and hard to notice - what the app tells a user
// when either is missing. A hint that says "sudo apt install wireplumber" on a
// machine with no apt is not a hint; it is a reason to stop reading hints.

#include <QtTest>

#include "platform/win/WinSound.h"
#include "core/settings.h"

#include <QDir>
#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
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

    /// A cue nobody hears is not a cue - and "nobody hears it" is measurable
    /// off the file, without ears and without a speaker: a RIFF/PCM file
    /// carries its own length and its own energy.
    ///
    /// Measured off C:\Windows\Media on this machine (2026-11-09), of the files
    /// this profile pointed at before the row below existed:
    ///   Windows Menu Command.wav    0.15 s  rms 0.001   <- the start cue
    ///   Windows Feed Discovered.wav 0.11 s  rms 0.003
    ///   Windows Ding.wav            1.09 s  rms 0.009
    ///   Windows Notify System Generic.wav  1.29 s  rms 0.058  <- the finish cue
    /// The pair the user described as "a mosquito squeak" under a real chime:
    /// the start cue is a quarter of the length and a fiftieth of the energy of
    /// the finish cue. It is not a taste question, and the old candidate list
    /// was picked by reading folder names, which is how it stayed invisible.
    void theChosenCuesAreFilesAPersonCanHear()
    {
        // The floor is a claim, so it is named: 0.25 s is the shortest length
        // that survives being heard over a fan (the finish cue of the same
        // profile is 5x that), and rms 0.02 (-34 dBFS) is the level below which
        // Windows' own default volume puts a file under room noise - the cues
        // this machine ships all sit between 0.03 and 0.07.
        const double minSeconds = 0.25;
        const double minRms = 0.02;

        const QString media = windowsMediaDir();
        if (media.isEmpty() || !QFileInfo::exists(media)) {
            QSKIP("no %WINDIR%\\Media on this machine - no bytes to measure");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings s(dir.path() + QStringLiteral("/config.ini"));
        SoundPlayer player(&s, {}, {media}, false, {}, nullptr, windowsSoundProfile());

        int measured = 0;
        for (SoundEvent e : {SoundEvent::Started, SoundEvent::Finished,
                             SoundEvent::Failed}) {
            const QString file = player.resolve(e);
            QVERIFY2(!file.isEmpty(), "the cue does not resolve on this machine");
            const WavFacts facts = measureWav(file);
            if (!facts.parsed) {
                continue;      // not PCM16: this row cannot speak about it
            }
            ++measured;
            const QString what = QStringLiteral("%1 (%2 s, rms %3)")
                .arg(QFileInfo(file).fileName())
                .arg(QString::number(facts.seconds, 'f', 2))
                .arg(QString::number(facts.rms, 'f', 4));
            QVERIFY2(facts.seconds >= minSeconds,
                     qPrintable(QStringLiteral("cue too short to be heard: ") + what));
            QVERIFY2(facts.rms >= minRms,
                     qPrintable(QStringLiteral("cue too quiet to be heard: ") + what));
        }
        QVERIFY2(measured > 0, "no cue on this machine is a PCM16 wav to measure");
    }

private:
    struct WavFacts
    {
        bool parsed = false;
        double seconds = 0.0;
        double rms = -1.0;
    };

    /// The two facts a sound file has to answer for a cue: how long it is, and
    /// how much energy it carries. Reads the RIFF chunks; anything that is not
    /// PCM16 comes back unparsed rather than guessed, because a row that
    /// invents a number is worse than a row that skips.
    static WavFacts measureWav(const QString &path)
    {
        WavFacts f;
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            return f;
        }
        const QByteArray blob = in.readAll();
        QDataStream r(blob);
        r.setByteOrder(QDataStream::LittleEndian);
        quint32 magic = 0, riffSize = 0, wave = 0;
        // RIFF, then the file size, then WAVE: the field between the two
        // signatures is a length, and reading it as the form name is how the
        // first draft of this function returned "not a wav" for every wav on
        // the machine (0x00006604 is a file size, not "WAVE").
        r >> magic >> riffSize >> wave;
        if (magic != 0x46464952 /* "RIFF" */ || wave != 0x45564157 /* "WAVE" */) {
            return f;
        }
        quint16 channels = 0, bits = 0, blockAlign = 0;
        quint32 rate = 0;
        bool haveFmt = false;
        r.device()->seek(12);
        while (!r.atEnd()) {
            quint32 id = 0, size = 0;
            r >> id >> size;
            const quint32 body = quint32(r.device()->pos());
            if (id == 0x20746d66 /* "fmt " */) {
                // All seven fields, in order, or every number after them is
                // read two bytes early - the first draft of this function did
                // that, and reported 16-bit files as 44100 Hz mono of unknown
                // width.
                quint16 format = 0, blockAlignUnused = 0;
                quint32 avgBytesPerSec = 0;
                r.device()->seek(body);
                r >> format >> channels >> rate >> avgBytesPerSec
                  >> blockAlignUnused >> bits;
                blockAlign = blockAlignUnused;
                if (format != 1) {
                    return f;              // not PCM: this row has no opinion
                }
                haveFmt = true;
            } else if (id == 0x61746164 /* "data " */) {
                if (!haveFmt || bits != 16 || channels == 0 || rate == 0) {
                    return f;
                }
                r.device()->seek(body);
                const QByteArray pcm = r.device()->read(
                    qMin<qint64>(size, 8 * 1024 * 1024));
                const int n = int(pcm.size() / 2);
                if (n <= 0) {
                    return f;
                }
                const auto *v = reinterpret_cast<const quint16 *>(pcm.constData());
                double sum = 0.0;
                for (int i = 0; i < n; ++i) {
                    const double s = (v[i] ^ 0x8000) - 0x8000;   // signed, no casts
                    sum += s * s;
                }
                f.rms = std::sqrt(sum / n) / 32768.0;
                f.seconds = double(pcm.size()) / (channels * rate * 2.0);
                f.parsed = true;
                return f;
            }
            r.device()->seek(body + size + (size & 1));   // chunks are word-aligned
        }
        return f;
    }

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
