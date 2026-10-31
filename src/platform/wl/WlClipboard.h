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
