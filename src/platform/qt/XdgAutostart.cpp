#include "XdgAutostart.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

namespace lt {

XdgAutostart::XdgAutostart(const QString &desktopId, const QString &execPath)
    : m_desktopId(desktopId)
    , m_execPath(execPath)
{
}

QString XdgAutostart::filePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/%1.desktop").arg(m_desktopId);
}

bool XdgAutostart::isEnabled() const
{
    return QFile::exists(filePath());
}

bool XdgAutostart::setEnabled(bool on)
{
    const QString path = filePath();
    if (!on) {
        return !QFile::exists(path) || QFile::remove(path);
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
