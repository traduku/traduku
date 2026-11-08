// The Windows sound profile: which .wav files, which player, and - the part
// that is easy to get wrong and hard to notice - what the app tells a user
// when either is missing. A hint that says "sudo apt install wireplumber" on a
// machine with no apt is not a hint; it is a reason to stop reading hints.

#include <QtTest>

#include "platform/win/WinSound.h"
#include "core/settings.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

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
};

QTEST_GUILESS_MAIN(TestWinSound)
#include "tst_winsound.moc"
