// traduku: GNOME shell around the platform-agnostic core.
//
// Composition only: settings, backends (clipboard/gesture/notifier/sounds/
// autostart), the AppController and the tray. No business logic here.
//
// Deliberately NO KF6 (GNOME_PLAN.md D3): GNOME ships none of it, and every
// layer this shell needs - tray (SNI + dbusmenu), notifications (FDO),
// global shortcut (gsettings), gesture (/dev/input), clipboard (wl-clipboard)
// - is plain D-Bus/ioctl/exec, so the GNOME build links Qt and nothing else.
//
// Stage status (GNOME_PLAN.md §9): this is the Stage 1 skeleton. The CLI
// modes (--translate, --selftest, --backend-info) are complete; the tray
// mode is refused until the gesture/notifier/tray layers land (Stages 3, 2,
// 5) - refusing loudly beats starting an app that silently never reacts to
// Ctrl+C+C.

#include "app/BackendInfo.h"
#include "app/SelfTest.h"
#include "app/cli.h"
#include "core/settings.h"
#include "platform/qt/SessionInfo.h"
#include "platform/wl/WlClipboard.h"
#include "platform/wl/ClipboardFactory.h"

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QTextStream>

#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace lt;

namespace {

/// Which QApplication flavour does this invocation need? Widgets require a
/// QApplication, and one cannot be created after a QGuiApplication, so the
/// decision has to be made before the first app object exists (from argv,
/// because QCoreApplication::arguments() needs an instance). Console-only
/// modes must not need a display: `traduku --backend-info` has to answer on
/// a machine where nothing graphical runs yet.
bool cliNeedsWidgets(const QStringList &argv)
{
    static const QStringList cliFlags = {
        QStringLiteral("--translate"), QStringLiteral("-t"),
        QStringLiteral("--selftest"), QStringLiteral("--backend-info"),
        QStringLiteral("--text"), QStringLiteral("--live"),
        QStringLiteral("--help"), QStringLiteral("-h"),
        QStringLiteral("--version"),
    };
    for (const QString &a : argv) {
        if (cliFlags.contains(a)) {
            return a == QLatin1String("--selftest");   // only selftest shows a window
        }
    }
    return true;                                       // no mode flag: tray mode
}

/// Can the keyboard be read for the /dev/input gesture? Empty answer means
/// "nothing to complain about". Stage 3 moves this behind an injectable
/// DeviceFinder in platform/evdev (unit-testable); it only ever opens nodes
/// read-only, never EVIOCGRABs, so copying is never disturbed.
QString inputAccessStatus()
{
    QDir dir(QStringLiteral("/dev/input"));
    const QStringList entries =
        dir.entryList({QStringLiteral("event*")}, QDir::System, QDir::Name);
    if (entries.isEmpty()) {
        return QStringLiteral("no /dev/input/event* devices at all");
    }
    int readableKeyboards = 0, denied = 0;
    for (const QString &entry : entries) {
        const QString path = dir.absoluteFilePath(entry);
        const int fd = ::open(qPrintable(path), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            if (errno == EACCES || errno == EPERM) {
                ++denied;
            }
            continue;
        }
        unsigned long evBits[1] = {};
        // EVIOCGBIT returns the number of BYTES copied, not 0.
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0
            && (evBits[0] & (1UL << EV_KEY))) {
            unsigned long keyBits[12] = {};
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
                const auto bit = [](const unsigned long *b, int code) {
                    return (b[code / 64] >> (code % 64)) & 1UL;
                };
                if (bit(keyBits, KEY_C)
                    && (bit(keyBits, KEY_LEFTCTRL) || bit(keyBits, KEY_RIGHTCTRL))) {
                    ++readableKeyboards;
                }
            }
        }
        ::close(fd);
    }
    if (readableKeyboards > 0) {
        return QString();
    }
    return denied > 0
               ? QStringLiteral("%1 of %2 input nodes are not readable by this user")
                     .arg(denied).arg(entries.size())
               : QStringLiteral("no device advertises KEY_C + KEY_CTRL");
}

BackendReport collectBackends()
{
    BackendReport r;
    r.shell = QStringLiteral("gnome");
    // Under a forced offscreen QPA (CI, --selftest on a headless box) the
    // platform name says nothing about the real session, so fall back to
    // XDG_SESSION_TYPE - tests/e2e asserts on this line.
    // Session (not QPA plugin - see platform/qt/SessionInfo.h): a forced
    // offscreen QPA must not make a Wayland box look like it has no Wayland.
    r.sessionType = sessionType();
    if (QGuiApplication::platformName() == QLatin1String("offscreen")
        && !r.sessionType.isEmpty()) {
        r.sessionType = QStringLiteral("%1 (qpa=offscreen)").arg(r.sessionType);
    }
    if (r.sessionType.isEmpty()) {
        r.sessionType = QGuiApplication::platformName();   // last resort
    }
    r.desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");

    // Named without spawning a subprocess: --backend-info must also answer
    // on a machine where the clipboard is unreachable.
    const bool wayland = isWaylandSession();
    if (wayland && WlClipboard::isUsable({}, nullptr)) {
        r.clipboard = QStringLiteral("wl-clipboard");
    } else {
        auto cb = createClipboard(r.sessionType, {}, QGuiApplication::clipboard());
        r.clipboard = cb->backendName();
        r.clipboardHint = cb->diagnostic();
    }

    // gesture/notifier/tray: Stages 3, 2, 5 fill these in. Empty prints as
    // "none", which is the truth and what tests/e2e greps for.
    r.gestureSource = QString();
    r.hotkeyChosen = QString();
    r.inputAccess = inputAccessStatus();
    r.notifier = QString();
    r.tray = QString();
    return r;
}

int doTranslate(QCoreApplication &app, Settings &settings, const CliOptions &cli)
{
    QTextStream out(stdout);
    // Chosen by session: under a forced offscreen QPA the Qt clipboard
    // reads nothing on Wayland, while wl-paste works.
    auto clipboard = createClipboard(sessionType(), {}, QGuiApplication::clipboard());
    const QString text =
        (cli.text.isNull() ? clipboard->text() : cli.text).trimmed();
    if (text.isEmpty()) {
        // The backend knows when it is structurally blind (missing
        // wl-clipboard on Wayland): say that instead of blaming the copy.
        const QString hint = clipboard->diagnostic();
        QTextStream(stderr) << (hint.isEmpty()
                                    ? QStringLiteral("traduku: nothing to translate "
                                                     "(clipboard is empty).\n")
                                    : hint + QLatin1Char('\n'));
        return 1;
    }
    int rc = 1;
    runOneShotTranslation(&settings, text,
                          [&out](const QString &chunk) {
                              out << chunk;
                              out.flush();          // stream into the pipe
                          },
                          [&rc](int code, const QString &error) {
                              rc = code;
                              if (code == 0) {
                                  QTextStream(stdout) << '\n';
                              } else {
                                  QTextStream(stderr) << "traduku: " << error << '\n';
                              }
                              qApp->quit();
                          });
    app.exec();
    return rc;
}

} // namespace

int main(int argc, char **argv)
{
    QStringList argvList;
    argvList.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        argvList << QString::fromLocal8Bit(argv[i]);
    }

    std::unique_ptr<QCoreApplication> app(
        cliNeedsWidgets(argvList)
            ? static_cast<QCoreApplication *>(new QApplication(argc, argv))
            : static_cast<QCoreApplication *>(new QGuiApplication(argc, argv)));
    if (auto *widgetApp = qobject_cast<QApplication *>(app.get())) {
        widgetApp->setQuitOnLastWindowClosed(false);   // tray-only app
    }
    configureApplication(*app, QStringLiteral("gnome"), QStringLiteral(LT_VERSION));

    const CliOptions cli = parseCommandLine(*app, argvList, QStringLiteral("gnome"));
    QTextStream out(stdout), err(stderr);

    if (cli.wantsHelp) {
        out << cliHelpText(*app, QStringLiteral("gnome")) << '\n';
        return 0;
    }
    if (cli.wantsVersion) {
        out << QStringLiteral("traduku %1 (%2 shell)\n")
                   .arg(QCoreApplication::applicationVersion(),
                        prettyShellName(QStringLiteral("gnome")));
        return 0;
    }
    if (cli.exitCode != 0) {
        for (const QString &e : cli.errors) {
            err << "traduku: " << e << '\n';
        }
        err << "try --help\n";
        return cli.exitCode;
    }

    Settings settings;

    switch (cli.mode) {
    case CliOptions::Mode::BackendInfo:
        out << formatBackendInfo(collectBackends());
        return 0;
    case CliOptions::Mode::SelfTest:
        return runSelfTest(out, cli.live) == 0 ? 0 : 1;
    case CliOptions::Mode::Translate:
        return doTranslate(*app, settings, cli);
    case CliOptions::Mode::Tray:
        break;
    }

    // ---- tray mode ------------------------------------------------------
    // Refused until the gesture (Stage 3), notifier (Stage 2) and tray
    // (Stage 5) backends exist. --backend-info explains the state and
    // --translate is fully usable meanwhile.
    err << "traduku: the GNOME tray/gesture backends are not built yet "
           "(GNOME_PLAN.md Stages 2/3/5).\n"
           "       run --backend-info to see what works, or use "
           "--translate/--selftest.\n";
    err << formatBackendInfo(collectBackends());
    return 5;
}
