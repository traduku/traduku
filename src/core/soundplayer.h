#pragma once

#include <QStringList>
#include <QObject>
#include <functional>

namespace lt {

class Settings;

/// Start / finish / error cues from the freedesktop sound theme, played on
/// every translation so the user can track progress without watching the
/// screen (as in the prototype). Search order: custom dirs (e.g. app-local
/// bundled sounds, XDG data dirs) then the system theme
/// /usr/share/sounds/freedesktop/stereo.
enum class SoundEvent { Started, Finished, Failed };

/*!
 * Playback backends are injectable (unit tests) and, by default, a chain
 * of detached audio players (paplay -> pw-play -> aplay). Missing sound
 * files are silently skipped - sound is an aid, not a hard dependency.
 */
class SoundPlayer : public QObject
{
    Q_OBJECT
public:
    using Player = std::function<bool(const QString &filePath)>;

    /// includeSystemThemeDirs=false restricts lookup to searchDirs only
    /// (used by tests; production keeps the full XDG theme chain).
    explicit SoundPlayer(Settings *settings,
                         Player singlePlayer = {},
                         const QStringList &searchDirs = {},
                         bool includeSystemThemeDirs = true,
                         QObject *parent = nullptr);

    static QString fileName(SoundEvent event);

    /// Overrides the default backend chain (paplay/pw-play/aplay).
    void setBackendChain(QVector<Player> chain);

    /// First existing candidate path, empty when unresolvable.
    QString resolve(SoundEvent event) const;

    void play(SoundEvent event);

private:
    static QVector<Player> defaultChain();

    Settings *m_settings;
    QStringList m_dirs;
    QVector<Player> m_chain;
};

} // namespace lt
