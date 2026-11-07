#include "X11Clipboard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QProcessEnvironment>

#include <unistd.h>

namespace lt {
namespace X11Session {

namespace {

QString normalizeDisplay(QString d)
{
    d = d.trimmed();
    if (d.isEmpty()) {
        return d;
    }
    // ":1.0" and ":1" name the same server for our purposes; keep the form
    // the caller can hand to XOpenDisplay either way.
    return d;
}

QString displayBase(const QString &d)
{
    return d.section(QLatin1Char('.'), 0, 0);      // ":1.0" -> ":1"
}

bool isXwaylandCmdline(const QString &cmdline)
{
    const QString first = cmdline.section(QLatin1Char(' '), 0, 0);
    return QFileInfo(first).fileName().compare(QLatin1String("Xwayland"),
                                               Qt::CaseInsensitive) == 0;
}

} // namespace

QString displayFromEnvironment(const QStringList &envEntries)
{
    for (const QString &entry : envEntries) {
        if (entry.startsWith(QLatin1String("DISPLAY="))) {
            return normalizeDisplay(entry.mid(8));
        }
    }
    return QString();
}

QString displayOfXwayland(const QStringList &procCmdlines)
{
    static const QRegularExpression displayArg(QStringLiteral("^:[0-9]+(\\.[0-9]+)?$"));
    for (const QString &cmdline : procCmdlines) {
        if (!isXwaylandCmdline(cmdline)) {
            continue;                               // a grep, an editor, Xwayland.c
        }
        const QStringList args = cmdline.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int i = 0; i < args.size(); ++i) {
            if (args.at(i) == QLatin1String("-display") && i + 1 < args.size()) {
                return normalizeDisplay(args.at(i + 1));
            }
            if (displayArg.match(args.at(i)).hasMatch()) {
                return normalizeDisplay(args.at(i));
            }
        }
    }
    return QString();
}

QString xauthorityFromEnviron(const QByteArray &environ)
{
    static const QByteArray key = QByteArrayLiteral("XAUTHORITY=");
    const QList<QByteArray> entries = environ.split('\0');
    for (const QByteArray &entry : entries) {
        // Exact key: MYXAUTHORITY or FOO_XAUTHORITY belong to somebody else.
        if (entry.startsWith(key)) {
            return QString::fromLocal8Bit(entry.mid(key.size()));
        }
    }
    return QString();
}

QStringList candidateXauthorities(const QString &home, int uid,
                                  const QString &fromEnvironment)
{
    QStringList out;
    if (!fromEnvironment.isEmpty()) {
        out << fromEnvironment;
    }
    if (uid >= 0) {
        out << QStringLiteral("/run/user/%1/gdm/Xauthority").arg(uid);
    }
    if (!home.isEmpty()) {
        out << home + QLatin1String("/.Xauthority");
    }
    out.removeAll(QString());
    QStringList unique;
    for (const QString &c : out) {
        if (!unique.contains(c)) {
            unique << c;
        }
    }
    return unique;
}

QStringList readProcCmdlines()
{
    QStringList out;
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    out.reserve(entries.size());
    for (const QString &e : entries) {
        if (!e.contains(QRegularExpression(QStringLiteral("^[0-9]+$")))) {
            continue;
        }
        QFile f(QStringLiteral("/proc/%1/cmdline").arg(e));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;                                // gone, or not ours to read
        }
        QString cmd = QString::fromLocal8Bit(f.readAll());
        cmd.replace(QChar(QChar::Null), QLatin1Char(' '));
        if (!cmd.trimmed().isEmpty()) {
            out << cmd.trimmed();
        }
    }
    return out;
}

QByteArray readProcEnviron(int pid)
{
    QFile f(QStringLiteral("/proc/%1/environ").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

QString resolveDisplay()
{
    // "KEY=VALUE" strings straight from the process environment.
    const QStringList env = QProcessEnvironment::systemEnvironment().toStringList();
    const QString fromEnv = displayFromEnvironment(env);
    if (!fromEnv.isEmpty()) {
        return fromEnv;
    }
    // Started by a systemd user service or XDG autostart: no DISPLAY, but an
    // XWayland may well be running for the desktop.
    return displayOfXwayland(readProcCmdlines());
}

QString resolveXauthority(const QString &display)
{
    const QString wanted = displayBase(display);

    // The Xwayland serving that display knows the cookie file it was given.
    if (!wanted.isEmpty()) {
        QDir proc(QStringLiteral("/proc"));
        for (const QString &e : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            static const QRegularExpression numeric(QStringLiteral("^[0-9]+$"));
            if (!numeric.match(e).hasMatch()) {
                continue;
            }
            QFile cf(QStringLiteral("/proc/%1/cmdline").arg(e));
            if (!cf.open(QIODevice::ReadOnly)) {
                continue;
            }
            QString cmd = QString::fromLocal8Bit(cf.readAll());
            cmd.replace(QChar(QChar::Null), QLatin1Char(' '));
            cmd = cmd.trimmed();
            if (!isXwaylandCmdline(cmd) || displayBase(displayOfXwayland({cmd})) != wanted) {
                continue;
            }
            const QString cookie = xauthorityFromEnviron(readProcEnviron(e.toInt()));
            if (!cookie.isEmpty() && QFileInfo::exists(cookie)) {
                return cookie;
            }
        }
    }

    const QString fromEnv = qEnvironmentVariable("XAUTHORITY");
    const QStringList candidates = candidateXauthorities(
        QDir::homePath(), static_cast<int>(getuid()), fromEnv);
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    return QString();                                // server may allow local
}

} // namespace X11Session
} // namespace lt
