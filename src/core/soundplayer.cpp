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

QVector<SoundPlayer::Player> SoundPlayer::defaultChain(const ToolFinder &finder)
{
    QVector<Player> chain;
    for (const QString &prog : candidatePlayers()) {
        chain.append(detachedPlayer(prog, finder));
    }
    return chain;
}

SoundPlayer::SoundPlayer(Settings *settings, Player singlePlayer,
                         const QStringList &searchDirs,
                         bool includeSystemThemeDirs, ToolFinder finder,
                         QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_find(finder ? std::move(finder) : ToolFinder(detect))
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
    } else {
        m_chain = defaultChain(m_find);
    }
}

void SoundPlayer::setBackendChain(QVector<Player> chain)
{
    m_chain = std::move(chain);
}

QString SoundPlayer::resolve(SoundEvent event) const
{
    const QString name = fileName(event);
    for (const QString &dir : m_dirs) {
        const QString path = QDir(dir).filePath(name);
        if (QFile::exists(path)) {
            return path;
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
    for (const QString &prog : candidatePlayers()) {
        if (!m_find(prog).isEmpty()) {
            st.player = prog;
            break;
        }
    }
    if (st.file.isEmpty()) {
        st.problem = QStringLiteral("no sound theme: %1 was not found in %2 "
                                    "(install yaru-sound-theme / freedesktop-sound-theme, "
                                    "or point [Sounds] ThemeDir at a directory that has it)")
                        .arg(fileName(event),
                             m_dirs.isEmpty() ? QStringLiteral("the XDG sound dirs")
                                              : m_dirs.first());
    } else if (st.player.isEmpty()) {
        st.problem = QStringLiteral("nothing can play %1: none of %2 is installed "
                                    "(on PipeWire: sudo apt install wireplumber, or "
                                    "pulseaudio-utils for paplay)")
                        .arg(fileName(event), candidatePlayers().join(QStringLiteral(", ")));
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
