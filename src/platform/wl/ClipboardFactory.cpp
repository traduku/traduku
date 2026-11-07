#include "ClipboardFactory.h"

#include "platform/qt/QtClipboard.h"
#include "platform/x11/X11Clipboard.h"

#include <QClipboard>
#include <QGuiApplication>

namespace lt {

namespace {

// Measured on GNOME 50 (ROADMAP.md §2): a background wayland client
// receives no data offer, so Qt's clipboard reads nothing there - only
// wl-paste/wl-copy (or an XWayland connection) see the DE clipboard. A
// silently empty result blames the user's clipboard; say what is missing.
// Keep the actionable part in front: notification servers truncate.
QString missingToolsHint()
{
    return QStringLiteral(
        "Traduku cannot see the clipboard: the wl-clipboard tools are not "
        "installed (sudo apt install wl-clipboard, or pacman -S / dnf "
        "install wl-clipboard), and on Wayland a background app cannot read "
        "it through Qt. Install them and restart Traduku.");
}

} // namespace

ClipboardBackend chooseClipboard(bool waylandSession, bool haveWlClipboardTools,
                                bool haveX11Server, const QString &forced)
{
    // The order is the whole product decision, so it is stated as a function
    // of its inputs rather than buried in side effects. A test can write all
    // eight rows; it cannot write the machine it happens to run on.
    //
    // On wayland wl-clipboard comes first: the compositor owns the clipboard
    // and XWayland only holds a bridge of it. Without the tools - the Arch
    // case, where wl-clipboard lives only in the AUR - X11 still beats Qt,
    // because Qt's own xcb backend was measured turning a plain TEXT offer
    // into mojibake while our X11 reader negotiates the way wl-clipboard
    // does, so the two agree on what the selection actually says.
    //
    // A forced backend is honoured only when its prerequisite exists:
    // TRADUKU_CLIPBOARD is a user's debugging knob, and honoring it into a
    // backend that cannot work would produce an empty paste with no reason.
    const QString forcedLower = forced.toLower();
    if (forcedLower == QLatin1String("wl") && haveWlClipboardTools) {
        return ClipboardBackend::WlClipboardTool;
    }
    if (forcedLower == QLatin1String("x11") && haveX11Server) {
        return ClipboardBackend::X11;
    }
    if (forcedLower == QLatin1String("qt")) {
        return ClipboardBackend::Qt;
    }
    if (waylandSession && haveWlClipboardTools) {
        return ClipboardBackend::WlClipboardTool;
    }
    if (haveX11Server) {
        return ClipboardBackend::X11;
    }
    return ClipboardBackend::Qt;
}

QString forcedBackendProblem(const QString &chosenBackend, const QString &forced,
                            bool forcedIsUsable, const QString &reason)
{
    const QString forcedLower = forced.toLower();
    if (forcedLower.isEmpty() || forcedLower == chosenBackend) {
        return {};                     // honoured, or never asked for
    }
    if (forcedLower == QLatin1String("qt")) {
        return {};                     // Qt is always available
    }
    if (forcedLower == QLatin1String("wl")) {
        return QStringLiteral("TRADUKU_CLIPBOARD=wl could not be used: "
                              "wl-paste/wl-copy are not installed or do not run");
    }
    if (forcedLower == QLatin1String("x11")) {
        if (forcedIsUsable) {
            // The server is there and we still did not use it: that is a bug
            // in the choice, not a limitation of the machine, and the report
            // has to say so rather than let the user debug our logic.
            return QStringLiteral("TRADUKU_CLIPBOARD=x11 was ignored although an "
                                  "X server is reachable");
        }
        return QStringLiteral("TRADUKU_CLIPBOARD=x11 could not be used: %1")
            .arg(reason.isEmpty() ? QStringLiteral("no reason given") : reason);
    }
    return QStringLiteral("TRADUKU_CLIPBOARD=%1 is not a known backend "
                          "(wl, x11, qt)").arg(forced);
}

QString forcedBackendProblem(const QString &chosenBackend)
{
    const QString forced = qEnvironmentVariable("TRADUKU_CLIPBOARD").toLower();
    if (forced.isEmpty() || forced == chosenBackend || forced == QLatin1String("qt")) {
        return {};
    }
    if (forced == QLatin1String("x11")) {
        return forcedBackendProblem(chosenBackend, forced, X11Clipboard::isUsable(),
                                    X11Clipboard::unusabilityReason());
    }
    return forcedBackendProblem(chosenBackend, forced, false, QString());
}

std::unique_ptr<IClipboard> createClipboard(const QString &platformName,
                                            const WlClipboard::ToolFinder &finder,
                                            QClipboard *qtClipboard,
                                            const WlClipboard::ToolRunner &runner,
                                            const XAvailability &xAvailable)
{
    // The caller passes what it believes the session is, and the backend
    // report decorates that same string ("wayland (qpa=offscreen)") to be
    // honest about what it measured. An exact == here let that decoration
    // change which reader the app uses: a decorated wayland box silently lost
    // wl-clipboard. Match the first word; keep the decoration harmless.
    QString firstWord = platformName.section(QLatin1Char(' '), 0, 0);
    if (firstWord.startsWith(QLatin1Char('('))) {
        firstWord.clear();   // "(qpa=offscreen)" alone is not a session type
    }
    const bool wayland = firstWord.compare(QLatin1String("wayland"), Qt::CaseInsensitive) == 0;
    const bool haveTools = WlClipboard::isUsable(runner, finder);
    const bool haveX11 = xAvailable ? xAvailable() : X11Clipboard::isUsable();

    switch (chooseClipboard(wayland, haveTools, haveX11,
                            qEnvironmentVariable("TRADUKU_CLIPBOARD"))) {
    case ClipboardBackend::WlClipboardTool:
        return std::make_unique<WlClipboard>(runner, finder);
    case ClipboardBackend::X11:
        return std::make_unique<X11Clipboard>();
    case ClipboardBackend::Qt:
        break;
    }

    if (!qtClipboard) {
        qtClipboard = QGuiApplication::clipboard();
    }
    // The hint is a claim about the machine: "the wl-clipboard tools are not
    // installed". It belongs to that fact, not to "we are on wayland and ended
    // up in Qt" - which is also what the user asked for when they set
    // TRADUKU_CLIPBOARD=qt. On ubuntu-gnome the forced-Qt case told a user to
    // install a package they had installed, and a diagnostic that misfires
    // trains people to ignore the ones that are right.
    const bool userAskedForQt =
        qEnvironmentVariable("TRADUKU_CLIPBOARD").compare(QLatin1String("qt"),
                                                           Qt::CaseInsensitive) == 0;
    if (wayland && !haveTools && !userAskedForQt) {
        // Blind by construction - carry the instruction to install the tools.
        return std::make_unique<QtClipboard>(qtClipboard, missingToolsHint());
    }
    return std::make_unique<QtClipboard>(qtClipboard);
}

} // namespace lt
