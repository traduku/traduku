#include "WlClipboard.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTextCodec>

namespace lt {

namespace {
QString defaultFinder(const QString &tool)
{
    return QStandardPaths::findExecutable(tool);
}

int defaultRunner(const QString &prog, const QStringList &args,
                  const QByteArray &in, QByteArray *out)
{
    return WlClipboard::runTool(prog, args, in, out);
}
} // namespace

WlClipboard::WlClipboard(ToolRunner runner, ToolFinder finder)
    : m_run(runner ? std::move(runner) : defaultRunner)
    , m_find(finder ? std::move(finder) : defaultFinder)
{
}

bool WlClipboard::isUsable(ToolRunner, ToolFinder finder)
{
    if (!finder) {
        finder = defaultFinder;
    }
    return !finder(QStringLiteral("wl-paste")).isEmpty()
        && !finder(QStringLiteral("wl-copy")).isEmpty();
}

int WlClipboard::runTool(const QString &program, const QStringList &args,
                         const QByteArray &input, QByteArray *output)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(program, args);
    if (!p.waitForStarted(3000)) {
        return -1;
    }
    if (!input.isEmpty()) {
        p.write(input);
    }
    p.closeWriteChannel();
    if (!p.waitForFinished(5000)) {
        p.kill();
        p.waitForFinished(1000);
        return -2;
    }
    if (output) {
        *output = p.readAllStandardOutput();
    }
    return p.exitCode();
}

QString WlClipboard::pickBestType(const QStringList &offered)
{
    static const QStringList exact{QStringLiteral("text/plain;charset=utf-8"),
                                   QStringLiteral("text/plain;charset=UTF-8"),
                                   QStringLiteral("text/plain"),
                                   QStringLiteral("UTF8_STRING")};
    for (const QString &want : exact) {
        if (offered.contains(want)) {
            return want;
        }
    }
    // Any charset-tagged text/plain (decode per charset), TEXT last.
    QString fallback;
    for (const QString &t : offered) {
        const QString lower = t.toLower();
        if (lower.startsWith(QLatin1String("text/plain;charset="))) {
            return t;
        }
        if (fallback.isEmpty() && t == QLatin1String("TEXT")) {
            fallback = t;
        }
    }
    return fallback;
}

QString WlClipboard::decodeWithCharset(const QByteArray &data,
                                       const QString &mimeType)
{
    QString charset = QStringLiteral("UTF-8");
    const int semi = mimeType.indexOf(QLatin1Char(';'));
    if (semi >= 0) {
        const QString param = mimeType.mid(semi + 1).trimmed();
        if (param.startsWith(QLatin1String("charset="), Qt::CaseInsensitive)) {
            charset = param.mid(8).trimmed().toUpper();
            if (charset == QLatin1String("UTF8")) {
                charset = QStringLiteral("UTF-8");
            }
        }
    }
    if (charset == QLatin1String("UTF-8")) {
        return QString::fromUtf8(data);
    }
    if (charset == QLatin1String("US-ASCII")) {
        return QString::fromLatin1(data);
    }
    if (const QTextCodec *codec = QTextCodec::codecForName(charset.toLatin1())) {
        return codec->toUnicode(data);
    }
    return QString::fromUtf8(data);   // last resort, replacement chars
}

QString WlClipboard::text() const
{
    // -l lists every offered MIME type; then fetch the best text target
    // (the charset matters: browsers serving legacy-codec pages offer
    // charset-tagged text/plain variants).
    QByteArray list;
    if (m_run(QStringLiteral("wl-paste"), {QStringLiteral("-n"), QStringLiteral("-l")},
              QByteArray(), &list)
        != 0) {
        return QString();
    }
    QStringList offered = QString::fromUtf8(list)
                              .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString type = pickBestType(offered);
    if (type.isEmpty()) {
        return QString();
    }
    QByteArray out;
    const int rc = m_run(QStringLiteral("wl-paste"),
                         {QStringLiteral("-n"), QStringLiteral("-t"), type},
                         QByteArray(), &out);
    return rc == 0 ? decodeWithCharset(out, type) : QString();
}

bool WlClipboard::setText(const QString &text)
{
    // wl-copy daemonizes to keep clipboard ownership after we exit.
    const int rc = m_run(QStringLiteral("wl-copy"),
                         {QStringLiteral("-n"), QStringLiteral("-t"),
                          QStringLiteral("text/plain")},
                         text.toUtf8(), nullptr);
    return rc == 0;
}

} // namespace lt
