#include "WlClipboard.h"

#include "core/texttargets.h"

#include <QProcess>
#include <QStandardPaths>

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
    // The rule moved to core/texttargets: the X11 reader needs the same
    // order and must not depend on this module to get it. Kept as a name
    // because wl-paste speaks MIME and this is where MIME lives.
    return TextTargets::pickBestMime(offered);
}

QString WlClipboard::decodeWithCharset(const QByteArray &data,
                                       const QString &mimeType)
{
    return TextTargets::decodeWithCharset(data, mimeType);
}

QStringList WlClipboard::readTypes(const QStringList &offered)
{
    // Negotiate first, then distrust. A source that offers real MIME types
    // gets asked by name, because the charset in the name is what
    // decodeWithCharset needs; the untyped read closes the list for the owner
    // that advertises something unusable (Klipper: "jMIHoY"), which is the
    // only way its data is reachable at all.
    QStringList plan;
    const QString best = TextTargets::pickBestMime(offered);
    if (!best.isEmpty()) {
        plan << best;
    }
    plan << QString();
    return plan;
}

QString WlClipboard::text() const
{
    // -l lists every offered MIME type; then fetch the best text target
    // (the charset matters: browsers serving legacy-codec pages offer
    // charset-tagged text/plain variants). A failing -l is not an answer about
    // the clipboard, only about the offer list, so the plan still ends with
    // the untyped read.
    QByteArray list;
    QStringList offered;
    if (m_run(QStringLiteral("wl-paste"), {QStringLiteral("-n"), QStringLiteral("-l")},
              QByteArray(), &list)
        == 0) {
        offered = QString::fromUtf8(list)
                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    for (const QString &type : readTypes(offered)) {
        QByteArray out;
        QStringList args{QStringLiteral("-n")};
        if (!type.isEmpty()) {
            args << QStringLiteral("-t") << type;
        }
        if (m_run(QStringLiteral("wl-paste"), args, QByteArray(), &out) != 0) {
            continue;      // this offer is not fetchable; ask the next way
        }
        // No -t means no charset was named, and UTF-8 is what the Wayland
        // text/plain convention promises.
        const QString decoded = type.isEmpty()
                                    ? QString::fromUtf8(out)
                                    : TextTargets::decodeWithCharset(out, type);
        if (decoded.trimmed().isEmpty()) {
            continue;
        }
        return decoded;
    }
    return QString();
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
