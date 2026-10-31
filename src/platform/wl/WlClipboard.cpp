#include "WlClipboard.h"

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

QString WlClipboard::text() const
{
    // -n: no trailing newline; -t text/plain: text offers only
    QByteArray out;
    const int rc = m_run(QStringLiteral("wl-paste"),
                         {QStringLiteral("-n"), QStringLiteral("-t"),
                          QStringLiteral("text/plain")},
                         QByteArray(), &out);
    return rc == 0 ? QString::fromUtf8(out) : QString();
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
