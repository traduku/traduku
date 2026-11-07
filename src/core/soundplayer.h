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
    /// program -> absolute path ("" when not installed). Injectable so a test
    /// can describe a machine instead of lying about the one it runs on.
    using ToolFinder = std::function<QString(const QString &program)>;

    /// What `play()` would actually do, for --backend-info.
    struct Status {
        bool enabled = false;   // the user's switch
        QString file;           // resolved theme file ("" = not found)
        QString player;         // program that would run ("" = none installed)
        QString problem;        // what to fix; empty when nothing is wrong
    };

    /// includeSystemThemeDirs=false restricts lookup to searchDirs only
    /// (used by tests; production keeps the full XDG theme chain).
    explicit SoundPlayer(Settings *settings,
                         Player singlePlayer = {},
                         const QStringList &searchDirs = {},
                         bool includeSystemThemeDirs = true,
                         ToolFinder finder = {},
                         QObject *parent = nullptr);

    static QString fileName(SoundEvent event);
    virtual ~SoundPlayer() = default;

    /// Overrides the default backend chain (paplay/pw-play/aplay).
    void setBackendChain(QVector<Player> chain);

    /// First existing candidate path, empty when unresolvable.
    QString resolve(SoundEvent event) const;

    /// Sound is the one feature whose failure is invisible: the translation
    /// finishes, nothing chirps, and "switched off", "no sound theme" and "no
    /// player installed" are indistinguishable from the outside. On
    /// ubuntu-gnome only pw-play exists (measured: no paplay, no aplay), so
    /// even the list of candidates is something to report rather than assume.
    Status status(SoundEvent event) const;

    /// Programs tried, in order. Named because the advice has to name one.
    static const QStringList &candidatePlayers();

    virtual void play(SoundEvent event);

private:
    static QVector<Player> defaultChain(const ToolFinder &finder);

    Settings *m_settings;
    QStringList m_dirs;
    QVector<Player> m_chain;
    ToolFinder m_find;
};

} // namespace lt
