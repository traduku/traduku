#include <QtTest>
#include <QSignalSpy>

#include "core/settings.h"
#include "core/soundplayer.h"

using namespace lt;

class TestSoundPlayer : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    QString makeTheme(const QString &dirPath, QStringList files)
    {
        QDir().mkpath(dirPath);
        for (const QString &f : files) {
            QFile(QDir(dirPath).filePath(f)).open(QIODevice::WriteOnly);
        }
        return dirPath;
    }

private slots:
    void initTestCase()
    {
        // Isolate the XDG theme lookup: only the dirs passed in the ctor
        // (plus an empty data home) can resolve sounds during the test.
        qputenv("XDG_DATA_DIRS", QByteArray());
        qputenv("XDG_DATA_HOME", (m_dir.path() + "/data").toUtf8());
    }

    // ---- what --backend-info has to be able to say ----------------------
    // Sound is the one feature whose failure the user cannot see: the
    // translation happens, nothing chirps, and "is it off, is the theme
    // missing, or is there no player?" all look identical. On ubuntu-gnome
    // only pw-play is installed (no paplay, no aplay), which is exactly the
    // case a guess would get wrong.

    void statusNamesThePlayerThatWouldRun()
    {
        const QString theme = makeTheme(m_dir.path() + "/pipe",
                                       {"bell.oga", "complete.oga", "dialog-error.oga"});
        Settings s(m_dir.path() + "/pipe.ini");
        SoundPlayer p(&s, {}, {theme}, false,
                      [](const QString &prog) {
                          return prog == QLatin1String("pw-play")
                              ? QStringLiteral("/usr/bin/pw-play") : QString();
                      });
        const auto st = p.status(SoundEvent::Finished);
        QCOMPARE(st.player, QString("pw-play"));
        QCOMPARE(st.file, theme + QStringLiteral("/complete.oga"));
        QVERIFY2(st.problem.isEmpty(), qPrintable(st.problem));
    }

    void statusSaysWhenNothingCanPlay()
    {
        const QString theme = makeTheme(m_dir.path() + "/noplayer",
                                       {"complete.oga"});
        Settings s(m_dir.path() + "/noplayer.ini");
        SoundPlayer p(&s, {}, {theme}, false, [](const QString &) { return QString(); });
        const auto st = p.status(SoundEvent::Finished);
        QVERIFY(st.player.isEmpty());
        QVERIFY2(!st.problem.isEmpty(),
                 "a machine that cannot play the cue has to be told why");
        // The advice must name something a package manager knows.
        QVERIFY2(st.problem.contains(QStringLiteral("pw-play")),
                 qPrintable(QStringLiteral("the hint does not name a player: ") + st.problem));
    }

    void statusSaysWhenTheSoundThemeIsMissing()
    {
        makeTheme(m_dir.path() + "/emptytheme", {});
        Settings s(m_dir.path() + "/emptytheme.ini");
        SoundPlayer p(&s, {}, {m_dir.path() + "/emptytheme"}, false,
                      [](const QString &) { return QStringLiteral("/usr/bin/paplay"); });
        const auto st = p.status(SoundEvent::Finished);
        QVERIFY(st.file.isEmpty());
        QVERIFY2(st.problem.contains(QStringLiteral("complete.oga")),
                 qPrintable(QStringLiteral("the hint does not name the missing file: ")
                            + st.problem));
    }

    void statusDoesNotComplainAboutASwitchTheUserTurnedOff()
    {
        Settings s(m_dir.path() + "/muted.ini");
        s.setSoundsEnabled(false);
        SoundPlayer p(&s, {}, {m_dir.path() + "/nowhere"}, false,
                      [](const QString &) { return QString(); });
        const auto st = p.status(SoundEvent::Finished);
        QVERIFY(!st.enabled);
        QVERIFY2(st.problem.isEmpty(),
                 "sounds are off by choice; a wall of advice about a disabled "
                 "feature trains the user to ignore the advice that matters");
    }

    void mapsEventsToThemeFiles()
    {
        QCOMPARE(SoundPlayer::fileName(SoundEvent::Started), QString("bell.oga"));
        QCOMPARE(SoundPlayer::fileName(SoundEvent::Finished), QString("complete.oga"));
        QCOMPARE(SoundPlayer::fileName(SoundEvent::Failed), QString("dialog-error.oga"));
    }

    void playsResolvableSounds()
    {
        const QString theme = makeTheme(m_dir.path() + QStringLiteral("/stereo"),
                                        {"bell.oga", "complete.oga", "dialog-error.oga"});
        Settings s(m_dir.path() + QStringLiteral("/c.ini"));
        QStringList played;
        SoundPlayer p(&s, [&](const QString &file) {
            played << file;
            return true;
        }, {theme}, false);
        p.play(SoundEvent::Started);
        p.play(SoundEvent::Finished);
        p.play(SoundEvent::Failed);
        QCOMPARE(played.size(), 3);
        QCOMPARE(played.at(0), theme + QStringLiteral("/bell.oga"));
        QCOMPARE(played.at(1), theme + QStringLiteral("/complete.oga"));
        QCOMPARE(played.at(2), theme + QStringLiteral("/dialog-error.oga"));
    }

    void customDirWinsOverTheme()
    {
        const QString custom = makeTheme(m_dir.path() + QStringLiteral("/custom"), {"complete.oga"});
        const QString theme = makeTheme(m_dir.path() + QStringLiteral("/theme"),
                                        {"complete.oga", "bell.oga"});
        Settings s(m_dir.path() + QStringLiteral("/c2.ini"));
        QStringList played;
        SoundPlayer p(&s, [&](const QString &f) { played << f; return true; },
                      {custom, theme}, false);
        p.play(SoundEvent::Finished);   // in both dirs: first dir wins
        QCOMPARE(played.at(0), custom + QStringLiteral("/complete.oga"));
        // started exists only in the theme -> found there
        p.play(SoundEvent::Started);
        QCOMPARE(played.at(1), theme + QStringLiteral("/bell.oga"));
    }

    void unresolvableSoundIsSilentlySkipped()
    {
        Settings s(m_dir.path() + QStringLiteral("/c3.ini"));
        int calls = 0;
        SoundPlayer p(&s, [&](const QString &) { ++calls; return true; },
                      {m_dir.path() + QStringLiteral("/nowhere")}, false);
        p.play(SoundEvent::Started);
        QCOMPARE(calls, 0);
    }

    void disabledSettingStopsPlayback()
    {
        const QString theme = makeTheme(m_dir.path() + QStringLiteral("/st"), {"bell.oga"});
        Settings s(m_dir.path() + QStringLiteral("/c4.ini"));
        int calls = 0;
        SoundPlayer p(&s, [&](const QString &) { ++calls; return true; }, {theme}, false);
        p.play(SoundEvent::Started);
        QCOMPARE(calls, 1);
        s.setSoundsEnabled(false);
        p.play(SoundEvent::Started);
        QCOMPARE(calls, 1);
        s.setSoundsEnabled(true);
        p.play(SoundEvent::Started);
        QCOMPARE(calls, 2);
    }

    void backendChainFallsBack()
    {
        // First backend fails to start (no paplay), second succeeds:
        // the default chain must try the next candidate.
        const QString theme = makeTheme(m_dir.path() + QStringLiteral("/st2"), {"bell.oga"});
        Settings s(m_dir.path() + QStringLiteral("/c5.ini"));
        QStringList order;
        SoundPlayer p(&s, {}, {theme}, false);
        p.setBackendChain({
            [&](const QString &f) { order << QStringLiteral("a:") + f; return false; },
            [&](const QString &f) { order << QStringLiteral("b:") + f; return true; },
            [&](const QString &f) { order << QStringLiteral("c:") + f; return true; },
        });
        p.play(SoundEvent::Started);
        QCOMPARE(order.size(), 2);
        QVERIFY(order.at(0).startsWith(QStringLiteral("a:")));
        QVERIFY(order.at(1).startsWith(QStringLiteral("b:")));
    }
};

QTEST_GUILESS_MAIN(TestSoundPlayer)
#include "tst_soundplayer.moc"
