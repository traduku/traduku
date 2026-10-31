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

SoundPlayer::Player detachedPlayer(const QString &program)
{
    return [program](const QString &file) {
        const QString exe = detect(program);
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

QVector<SoundPlayer::Player> SoundPlayer::defaultChain()
{
    // PulseAudio/PipeWire native players first (they decode .oga);
    // aplay covers plain .wav themes.
    QVector<Player> chain;
    for (const QString &prog : {QStringLiteral("paplay"),
                                QStringLiteral("pw-play"),
                                QStringLiteral("aplay")}) {
        chain.append(detachedPlayer(prog));
    }
    return chain;
}

SoundPlayer::SoundPlayer(Settings *settings, Player singlePlayer,
                         const QStringList &searchDirs,
                         bool includeSystemThemeDirs, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
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
        m_chain = defaultChain();
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
