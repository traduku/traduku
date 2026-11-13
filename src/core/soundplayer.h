#pragma once

#include <QStringList>
#include <QVector>
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

    /*
     * Three things about sound are not the same on every OS: which files to
     * look for, which programs may play them, and what to tell a user when
     * either is missing. All three are *data*, so they travel as a profile
     * rather than as an `#ifdef` inside core - core is not allowed to know
     * which OS it is on (ROADMAP.md §7 W5 is the gate that enforces it).
     *
     * The advice strings are part of the profile for the same reason they are
     * part of the product: "install wireplumber" on a machine that has no
     * apt is not a hint, it is a reason to stop reading the hints.
     */
    struct Profile {
        Profile();                                  ///< the freedesktop default
        QString name;                               // "freedesktop" | "windows" | ...
        std::function<QStringList(SoundEvent)> fileNames;  // candidates, first hit wins
        QStringList players;                        // programs tried, in order
        /// How to try them. Empty = the default chain, which detached-launches
        /// each name through the ToolFinder - correct for paplay/pw-play/aplay,
        /// wrong for anything that plays in-process. A profile that has a
        /// player which is not a program supplies the chain here, so there is
        /// no way to forget to install it.
        std::function<QVector<Player>()> chain;
        QString missingThemeAdvice;
        QString missingPlayerAdvice;
    };

    /// includeSystemThemeDirs=false restricts lookup to searchDirs only
    /// (used by tests; production keeps the full XDG theme chain).
    explicit SoundPlayer(Settings *settings,
                         Player singlePlayer = {},
                         const QStringList &searchDirs = {},
                         bool includeSystemThemeDirs = true,
                         ToolFinder finder = {},
                         QObject *parent = nullptr,
                         Profile profile = {});

    static QString fileName(SoundEvent event);
    virtual ~SoundPlayer() = default;

    /// Overrides the default backend chain (paplay/pw-play/aplay).
    void setBackendChain(QVector<Player> chain);

    /// First existing candidate path, empty when unresolvable.
    QString resolve(SoundEvent event) const;

    /// The profile this player was built with ("freedesktop" by default).
    /// --backend-info prints its name: "which sound system is this" is a
    /// question about the machine, and the answer must come from the object
    /// that plays, not from a guess beside it.
    const Profile &profile() const { return m_profile; }

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
    static QVector<Player> defaultChain(const ToolFinder &finder,
                                       const QStringList &players);

    Settings *m_settings;
    QStringList m_dirs;
    QVector<Player> m_chain;
    ToolFinder m_find;
    Profile m_profile;
};

} // namespace lt
