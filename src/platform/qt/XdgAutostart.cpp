#include "XdgAutostart.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

namespace lt {

namespace {

/// Every XDG_CONFIG_DIRS entry except the writable one: /etc/xdg/autostart on
/// a normal system, and whatever the session set.
QStringList defaultSystemDirs()
{
    QStringList dirs;
    const QString writable =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const QString &dir : QStandardPaths::standardLocations(
             QStandardPaths::GenericConfigLocation)) {
        if (dir != writable) {
            dirs << dir;
        }
    }
    return dirs;
}

QString entryIn(const QString &dir, const QString &id)
{
    return QDir(dir).filePath(QStringLiteral("autostart/%1.desktop").arg(id));
}

/// A desktop file counts as enabled unless it says otherwise. Two spellings
/// mean "do not start": Hidden=true (the spec) and
/// X-GNOME-Autostart-enabled=false (what gnome-control-center's Apps panel
/// writes). Ignoring the second made our menu tick an entry the user had just
/// switched off in GNOME's own UI - two answers to one question.
bool entrySaysEnabled(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    bool enabled = true;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key.compare(QLatin1String("Hidden"), Qt::CaseInsensitive) == 0
            || key.compare(QLatin1String("X-GNOME-Autostart-enabled"),
                           Qt::CaseInsensitive) == 0) {
            if (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
                || value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
                const bool truthy = value.compare(QLatin1String("true"),
                                                  Qt::CaseInsensitive) == 0;
                // Hidden=true means off; X-GNOME-Autostart-enabled=false means off.
                const bool isHidden = key.compare(QLatin1String("Hidden"),
                                                  Qt::CaseInsensitive) == 0;
                if (truthy == isHidden) {
                    enabled = false;
                }
            }
        }
    }
    return enabled;
}

} // namespace

XdgAutostart::XdgAutostart(const QString &desktopId, const QString &execPath,
                           const QStringList &systemDirs)
    : m_systemDirs(systemDirs.isEmpty() ? defaultSystemDirs() : systemDirs)
    , m_desktopId(desktopId)
    , m_execPath(execPath)
{
}

QString XdgAutostart::filePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/%1.desktop").arg(m_desktopId);
}

bool XdgAutostart::isSystemInstalled() const
{
    for (const QString &dir : m_systemDirs) {
        if (QFile::exists(entryIn(dir, m_desktopId))) {
            return true;
        }
    }
    return false;
}

bool XdgAutostart::isEnabled() const
{
    const QString user = filePath();
    if (QFile::exists(user)) {
        return entrySaysEnabled(user);      // shadows every system entry
    }
    for (const QString &dir : m_systemDirs) {
        if (entrySaysEnabled(entryIn(dir, m_desktopId))) {
            return true;
        }
    }
    return false;
}

QString XdgAutostart::diagnostic() const
{
    const QString user = filePath();
    if (QFile::exists(user)) {
        return entrySaysEnabled(user)
            ? QStringLiteral("xdg: enabled by %1").arg(user)
            : QStringLiteral("xdg: switched off by %1").arg(user);
    }
    for (const QString &dir : m_systemDirs) {
        const QString sys = entryIn(dir, m_desktopId);
        if (entrySaysEnabled(sys)) {
            return QStringLiteral("xdg: enabled by the package (%1)").arg(sys);
        }
    }
    return QStringLiteral("xdg: no autostart entry");
}

bool XdgAutostart::setEnabled(bool on)
{
    const QString path = filePath();
    if (!on) {
        if (!isSystemInstalled()) {
            return !QFile::exists(path) || QFile::remove(path);
        }
        // Removing our own file would not disable anything: the entry that
        // starts the app is the one in /etc/xdg. Write the shadow instead -
        // and write both spellings of "off", because gnome-control-center and
        // the spec disagree about which one they read.
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        QTextStream ts(&f);
        ts << "[Desktop Entry]\n"
              "Type=Application\n"
              "Name=Traduku\n"
           << "Exec=" << m_execPath << "\n"
              "Hidden=true\n"
              "X-GNOME-Autostart-enabled=false\n";
        f.flush();
        return !f.error();
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream ts(&f);
    ts << "[Desktop Entry]\n"
          "Type=Application\n"
          "Name=Traduku\n"
          "GenericName=Clipboard translator\n"
          "Comment=Translate the clipboard with an LLM (double Ctrl+C)\n"
       << "Exec=" << m_execPath << "\n"
          "Icon=accessories-dictionary\n"
          "Terminal=false\n"
          "Categories=Utility;Translation;\n"
          "X-KDE-autostart-after=panel\n"
          "NoDisplay=true\n";
    f.flush();
    return !f.error();
}

} // namespace lt
