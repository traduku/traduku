#include "soundplayer.h"

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace lt {

namespace {
QString detect(const QString &program)
{
    return QStandardPaths::findExecutable(program);
}

SoundPlayer::Player detachedPlayer(const QString &program,
                                   const SoundPlayer::ToolFinder &find)
{
    return [program, find](const QString &file) {
        const QString exe = find(program);
        if (exe.isEmpty()) {
            return false;
        }
        return QProcess::startDetached(exe, {file});
    };
}
} // namespace

QString SoundPlayer::fileName(SoundEvent event)
{
    switch (event) {
    case SoundEvent::Started:  return QStringLiteral("bell.oga");
    case SoundEvent::Finished: return QStringLiteral("complete.oga");
    case SoundEvent::Failed:   return QStringLiteral("dialog-error.oga");
    }
    return QString();
}

const QStringList &SoundPlayer::candidatePlayers()
{
    // PulseAudio/PipeWire native players first (they decode the .oga files the
    // freedesktop theme ships); aplay covers plain .wav themes. Order matters
    // on PipeWire, which is what both of our test desktops run: pw-play talks
    // to it directly, paplay needs the pipewire-pulse shim.
    static const QStringList players = {QStringLiteral("paplay"),
                                        QStringLiteral("pw-play"),
                                        QStringLiteral("aplay")};
    return players;
}

SoundPlayer::Profile::Profile()
    : name(QStringLiteral("freedesktop"))
    , fileNames([](SoundEvent event) {
          return QStringList{SoundPlayer::fileName(event)};
      })
    , players(candidatePlayers())
    , missingThemeAdvice(QStringLiteral(
          "no sound theme: %1 was not found in %2 (install yaru-sound-theme / "
          "freedesktop-sound-theme, or point [Sounds] ThemeDir at a directory "
          "that has it)"))
    , missingPlayerAdvice(QStringLiteral(
          "nothing can play %1: none of %2 is installed (on PipeWire: sudo apt "
          "install wireplumber, or pulseaudio-utils for paplay)"))
{
}

QVector<SoundPlayer::Player> SoundPlayer::defaultChain(const ToolFinder &finder,
                                                       const QStringList &players)
{
    QVector<Player> chain;
    for (const QString &prog : players) {
        chain.append(detachedPlayer(prog, finder));
    }
    return chain;
}

SoundPlayer::SoundPlayer(Settings *settings, Player singlePlayer,
                         const QStringList &searchDirs,
                         bool includeSystemThemeDirs, ToolFinder finder,
                         QObject *parent, Profile profile)
    : QObject(parent)
    , m_settings(settings)
    , m_find(finder ? std::move(finder) : ToolFinder(detect))
    , m_profile(std::move(profile))
{
    m_dirs = searchDirs;
    // Freedesktop theme locations (XDG_DATA_HOME first, then XDG_DATA_DIRS,
    // which by default includes /usr/share), after the caller-provided dirs.
    if (includeSystemThemeDirs) {
        for (const QString &base : QStandardPaths::standardLocations(
                 QStandardPaths::GenericDataLocation)) {
            m_dirs << base + QStringLiteral("/sounds/freedesktop/stereo");
        }
    }

    if (singlePlayer) {
        m_chain = {std::move(singlePlayer)};
    } else if (m_profile.chain) {
        m_chain = m_profile.chain();
    } else {
        m_chain = defaultChain(m_find, m_profile.players);
    }
}

void SoundPlayer::setBackendChain(QVector<Player> chain)
{
    m_chain = std::move(chain);
}

QString SoundPlayer::resolve(SoundEvent event) const
{
    // First hit wins, per directory, over the profile's candidates: a theme
    // (or %WINDIR%\Media) does not have to ship every one of the names we ask
    // for, and "no sound at all because one filename is missing" is not a
    // reason to leave the user mute.
    const QStringList names = m_profile.fileNames ? m_profile.fileNames(event)
                                                 : QStringList{fileName(event)};
    for (const QString &dir : m_dirs) {
        for (const QString &name : names) {
            const QString path = QDir(dir).filePath(name);
            if (QFile::exists(path)) {
                return path;
            }
        }
    }
    return QString();
}

SoundPlayer::Status SoundPlayer::status(SoundEvent event) const
{
    Status st;
    st.enabled = !m_settings || m_settings->soundsEnabled();
    if (!st.enabled) {
        // The user switched it off. Nothing is broken, and advice about a
        // feature someone turned off is noise that gets the real advice
        // ignored.
        return st;
    }
    st.file = resolve(event);
    for (const QString &prog : m_profile.players) {
        if (!m_find(prog).isEmpty()) {
            st.player = prog;
            break;
        }
    }
    const QStringList names = m_profile.fileNames ? m_profile.fileNames(event)
                                                 : QStringList{fileName(event)};
    const QString asked = names.isEmpty() ? QStringLiteral("(no candidates)")
                                          : names.first();
    if (st.file.isEmpty()) {
        st.problem = m_profile.missingThemeAdvice
            .arg(asked,
                 m_dirs.isEmpty() ? QStringLiteral("the XDG sound dirs")
                                  : m_dirs.first());
    } else if (st.player.isEmpty()) {
        st.problem = m_profile.missingPlayerAdvice
            .arg(st.file, m_profile.players.join(QStringLiteral(", ")));
    }
    return st;
}

void SoundPlayer::play(SoundEvent event)
{
    if (m_settings && !m_settings->soundsEnabled()) {
        return;
    }
    const QString path = resolve(event);
    if (path.isEmpty()) {
        return;
    }
    for (const Player &backend : m_chain) {
        if (backend(path)) {
            return;
        }
    }
}

} // namespace lt
