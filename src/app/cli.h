#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace lt {

class Settings;

/*!
 * Command line shared by every shell (kde/gnome). It used to live inside
 * src/app/kde/main.cpp; duplicating it per shell is how `traduku --version`
 * ended up printing no version at all (nobody called
 * QApplication::setApplicationVersion), so parsing now has one home and
 * tests of its own (tests/unit/tst_cli.cpp).
 */
struct CliOptions
{
    enum class Mode { Tray, Translate, SelfTest, BackendInfo, NotifyTest };

    Mode mode = Mode::Tray;
    QString text;              // --text; null means "read the clipboard"
    QString configPath;        // --config; empty = $TRADUKU_CONFIG or XDG
    bool live = false;         // --live, only meaningful with --selftest
    bool wantsHelp = false;
    bool wantsVersion = false;
    QStringList errors;
    /// Non-zero means main() must print `errors` (or nothing) and return.
    int exitCode = 0;
};

/// Application identity for QCoreApplication/Qt logging and --version.
/// `shell` is "kde" or "gnome"; it shows up in the display name so a
/// screenshot tells which build is running.
void configureApplication(QCoreApplication &app, const QString &shell,
                          const QString &version);

/// Parse `args` (including argv[0]). Never calls exit()/quit(): --help and
/// --version come back as flags, so callers (and tests) stay in control.
CliOptions parseCommandLine(QCoreApplication &app, const QStringList &args,
                            const QString &shell);

/// "GNOME" / "KDE Plasma" for display (applicationDisplayName lives on
/// QGuiApplication, and this parser must also work for a console-only
/// --backend-info run, so the pretty name is exposed, not stored).
QString prettyShellName(const QString &shell);

/// Text for --help (same thing the parser would have printed).
QString cliHelpText(QCoreApplication &app, const QString &shell);

/// One-shot translation for `traduku --translate`: streams `onDelta` per
/// chunk, then calls `onDone(exitCode, errorText)` exactly once. Deliberately
/// free of GUI, tray and clipboard - the caller decides where the text came
/// from, which is also what lets tests drive it against the mock SSE server.
void runOneShotTranslation(Settings *settings, const QString &text,
                           std::function<void(const QString &)> onDelta,
                           std::function<void(int, const QString &)> onDone);

} // namespace lt
