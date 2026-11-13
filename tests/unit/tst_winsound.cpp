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
#include <QVector>
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
    /// off the file, without ears and without a speaker.
    ///
    /// The metric is the loudest 100 ms of the file, not its average.
    /// The first version of this row averaged the whole file, which is a
    /// statement about the sound's energy and not about the ear: a bell rings
    /// and decays, so the decay is counted as if it were silence. That metric
    /// called the cue a person chose by ear "three times too quiet"
    /// (Windows Notify.wav: 0.0117 averaged over 1.29 s, 0.0262 over its loudest
    /// 100 ms), and called the cue the same person rejected - "a mosquito
    /// squeak" - about as quiet (Windows Menu Command.wav: 0.0011 averaged,
    /// 0.0013 over its loudest 100 ms). It is the second number that agrees
    /// with the ear, and `theLoudnessMetricIsAnchoredOnTheEarVerdicts` below
    /// pins that agreement, so the floor cannot be moved by taste.
    void theChosenCuesAreFilesAPersonCanHear()
    {
        // The floors are claims, so they are named, and both sit on the side of
        // the measurements that the ear rejected: 0.25 s is above the 0.11 s of
        // the squeak, 0.02 is below the 0.0262 of the accepted cue and above
        // the 0.0013 of the rejected one.
        const double minAudibleSeconds = 0.25;
        const double minLoudness = 0.02;

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
            const QString what = QStringLiteral("%1 (heard for %2 s, loudness %3)")
                .arg(QFileInfo(file).fileName())
                .arg(QString::number(facts.audible, 'f', 2))
                .arg(QString::number(facts.loudness, 'f', 4));
            QVERIFY2(facts.audible >= minAudibleSeconds,
                     qPrintable(QStringLiteral("cue too short to be heard: ") + what));
            QVERIFY2(facts.loudness >= minLoudness,
                     qPrintable(QStringLiteral("cue too quiet to be heard: ") + what));
        }
        QVERIFY2(measured > 0, "no cue on this machine is a PCM16 wav to measure");
    }

    /// The negative control of the row above, written as a product rule.
    ///
    /// A loudness floor with no anchored samples is a number somebody picked:
    /// the first version of this gate rejected the cue a person chose and
    /// accepted the cue a person rejected. So the metric is required to order
    /// the two verdicts the way the ear ordered them, by a margin, on this
    /// machine's own files - and if a future edit moves the floor to make a
    /// cue fit, this row says which verdict it was moved away from.
    void theLoudnessMetricIsAnchoredOnTheEarVerdicts()
    {
        const QString media = windowsMediaDir();
        if (media.isEmpty() || !QFileInfo::exists(media)) {
            QSKIP("no %WINDIR%\\Media on this machine - no bytes to measure");
        }
        const WavFacts squeak = measureWav(QDir(media).filePath(
            QStringLiteral("Windows Menu Command.wav")));    // "писк комара"
        const WavFacts chosen = measureWav(QDir(media).filePath(
            QStringLiteral("Windows Notify.wav")));           // chosen by ear
        if (!squeak.parsed || !chosen.parsed) {
            QSKIP("this Windows does not ship both files to compare");
        }
        QVERIFY2(chosen.loudness > squeak.loudness * 4.0,
                 qPrintable(QStringLiteral("the metric does not separate the cue "
                                            "the ear chose from the one it "
                                            "rejected: chosen ")
                            + QString::number(chosen.loudness, 'f', 4)
                            + QStringLiteral(" vs squeak ")
                            + QString::number(squeak.loudness, 'f', 4)));
        QVERIFY2(chosen.audible > squeak.audible * 4.0,
                 "the chosen cue is not longer, in heard seconds, than the squeak");
    }

    /// Which file the start cue is, on a machine that has it.
    ///
    /// The choice is the user's (2026-11-09, after an audition of four
    /// candidates played on a live desktop), and it is recorded as a product
    /// decision, so that the next person who "improves" the list by picking a
    /// louder file has to answer the ear that picked this one.
    void theStartCueIsTheOneTheEarChose()
    {
        const QString media = windowsMediaDir();
        if (media.isEmpty()) {
            QSKIP("no %WINDIR%\\Media on this machine");
        }
        const QStringList names = windowsSoundCandidates(SoundEvent::Started);
        QVERIFY(!names.isEmpty());
        QString resolved;
        for (const QString &name : names) {
            if (QFile::exists(QDir(media).filePath(name))) {
                resolved = name;
                break;
            }
        }
        if (resolved.isEmpty()) {
            QSKIP("this Windows has none of the start-cue files");
        }
        QCOMPARE(resolved, QStringLiteral("Windows Notify.wav"));
    }

private:
    struct WavFacts
    {
        bool parsed = false;
        double seconds = 0.0;   ///< the whole file
        double audible = 0.0;   ///< first to last frame above the noise gate
        double loudness = 0.0;  ///< rms of the loudest 100 ms - what an ear hears
        double rms = 0.0;       ///< over the whole file; see the row above
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
        quint16 channels = 0, bits = 0;
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
                quint16 format = 0, blockAlign = 0;
                quint32 avgBytesPerSec = 0;
                r.device()->seek(body);
                r >> format >> channels >> rate >> avgBytesPerSec
                  >> blockAlign >> bits;
                if (format != 1) {
                    return f;              // not PCM: this row has no opinion
                }
                haveFmt = true;
            } else if (id == 0x61746164 /* "data" */) {
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
                // One channel is enough: every cue Windows ships is a stereo
                // pair of the same signal, and a window over a mono stream is
                // the same window with half the samples.
                QVector<double> s;
                s.reserve(n / channels);
                const auto *v = reinterpret_cast<const quint16 *>(pcm.constData());
                for (int i = 0; i < n; i += channels) {
                    s << ((double(int(v[i]) ^ 0x8000) - 32768.0) / 32768.0);
                }
                const int sn = s.size();
                const int window = qMax(1, int(rate / 10));          // 100 ms
                double sumAll = 0.0;
                for (double x : s) {
                    sumAll += x * x;
                }
                f.rms = std::sqrt(sumAll / sn);
                // The loudest 100 ms in the file, in 50 ms steps: the ear
                // answers to a cue's loudest moment, not to its average.
                double loud = 0.0;
                for (int i = 0; i + window <= sn; i += qMax(1, window / 2)) {
                    double sum = 0.0;
                    for (int k = i; k < i + window; ++k) {
                        sum += s.at(k) * s.at(k);
                    }
                    loud = qMax(loud, std::sqrt(sum / window));
                }
                f.loudness = loud;
                f.seconds = double(sn) / rate;
                // How long anything is actually in there: the span between the
                // first and the last sample above 2% of the loudest moment - a
                // noise gate, so a 2-second file whose tone is 150 ms long is
                // reported as 150 ms of cue.
                const double gate = 0.02 * loud;
                int first = -1, last = -1;
                for (int i = 0; i < sn; ++i) {
                    if (qAbs(s.at(i)) >= gate) {
                        if (first < 0) {
                            first = i;
                        }
                        last = i;
                    }
                }
                f.audible = first < 0 ? 0.0 : double(last - first) / rate;
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
