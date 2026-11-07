#pragma once

#include "interfaces/iclipboard.h"

#include <QByteArray>
#include <QStringList>
#include <functional>

namespace lt {

/*!
 * Wayland clipboard through the wl-clipboard tools (wl-paste / wl-copy):
 * a no-focus background app cannot own Wayland clipboard data through
 * Qt's API, the tools can. Process spawning is injected (ToolRunner) so
 * the logic is unit-testable and host-agnostic; process lookup is
 * injectable too (ToolFinder).
 */
class WlClipboard : public IClipboard
{
public:
    /// run(program, args, stdinData, stdoutOut) -> exit code
    using ToolRunner = std::function<int(const QString &, const QStringList &,
                                         const QByteArray &, QByteArray *)>;
    /// tool name -> absolute path (empty when missing)
    using ToolFinder = std::function<QString(const QString &)>;

    WlClipboard(ToolRunner runner = {}, ToolFinder finder = {});

    static bool isUsable(ToolRunner runner = {}, ToolFinder finder = {});

    /// MIME target negotiation (the qclipboard/KWrite behaviour): from the
    /// offers of a source app, prefer text/plain;charset=utf-8, then plain
    /// text/plain, then UTF8_STRING, then any charset-tagged text/plain
    /// (decoded per its charset), then TEXT; text/* only. Empty = no text.
    static QString pickBestType(const QStringList &offered);

    /// Decode payload bytes per the charset embedded in a MIME type
    /// (utf-8 default; windows-1251 / iso-8859-1 / ... via QTextCodec).
    static QString decodeWithCharset(const QByteArray &data, const QString &mimeType);

    /// What to ask `wl-paste` for, in order; an empty entry means "ask with no
    /// -t at all".
    ///
    /// The untyped read is not an optimization, it is the reason a clipboard
    /// with an owner that advertises nonsense is still readable. Measured on
    /// ubuntu-kde with Klipper in charge: `wl-paste -l` printed `jMIHoY` - not
    /// a MIME type at all - while `wl-paste` returned the text the user had
    /// copied. An offer list is another program's claim about itself; the
    /// bytes are the fact, and the reader has to go and get them.
    static QStringList readTypes(const QStringList &offered);

    QString text() const override;
    bool setText(const QString &text) override;
    QString backendName() const override { return QStringLiteral("wl-clipboard"); }

    /// Shared QProcess-backed runner (default for ToolRunner).
    static int runTool(const QString &program, const QStringList &args,
                       const QByteArray &input, QByteArray *output);

private:
    ToolRunner m_run;
    ToolFinder m_find;
};

} // namespace lt
