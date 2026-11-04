// clip-spike — Stage 0 diagnostic (NOT product code; see GNOME_PLAN.md D2).
//
// Question it answers: on THIS compositor, which available way actually
// READS the user's clipboard from a background (never-focused) client, and
// which one WRITES it? The answer picks the clipboard backend for the GNOME
// shell: QtClipboard, WlClipboard, the XWayland/X11 bridge, or the
// focus-window trick.
//
//   ./build/bin/clip-spike probe            # A, B, C, C2 (+E with --x11)
//   ./build/bin/clip-spike probe --x11 --xcb  # everything, incl. XWayland
//   ./build/bin/clip-spike seed "TEXT"      # every WRITE path
//
// Read it twice: once idle, once right after a real Ctrl+C in a text
// editor - that is the situation the gesture must survive.
#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QWindow>

#include <cstdio>
#include <memory>

#include <unistd.h>

#ifdef HAVE_X11
// X11 headers clash with Qt's (Status/None/True/False) - include LAST.
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace {

void say(const QString &s)
{
    std::fputs(qPrintable(s), stdout);
    std::fflush(stdout);
}

void line(const QString &name, bool ok, qint64 ms, const QString &detail)
{
    say(QStringLiteral("  %1 %2 %3ms  %4\n")
            .arg(ok ? QStringLiteral("OK  ") : QStringLiteral("FAIL"))
            .arg(name.leftJustified(24))
            .arg(QString::number(ms).rightJustified(5))
            .arg(detail));
}

/// Pump events for up to ms, watching for a clipboard data offer.
bool waitForDataChanged(QClipboard *cb, int ms)
{
    QEventLoop loop;
    bool got = false;
    QObject::connect(cb, &QClipboard::dataChanged, [&]() {
        got = true;
        loop.quit();
    });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return got;
}

struct ReadResult {
    bool ok = false;
    qint64 ms = 0;
    QString detail = QStringLiteral("no data");
};

ReadResult readQt(QClipboard *cb, bool activateWindow)
{
    ReadResult r;
    QElapsedTimer t;
    t.start();
    std::unique_ptr<QWindow> window;
    if (activateWindow) {
        window = std::make_unique<QWindow>();
        window->setTitle(QStringLiteral("traduku clip spike"));
        window->resize(320, 120);
        window->show();
        window->requestActivate();
        QEventLoop loop;
        QTimer::singleShot(350, &loop, &QEventLoop::quit);
        loop.exec();
    } else {
        waitForDataChanged(cb, 250);   // offers arrive on focus change only
    }
    const QString text = cb->text();
    r.ms = t.elapsed();
    r.ok = !text.isEmpty();
    r.detail = r.ok
        ? QStringLiteral("'%1' (%2 chars)").arg(text.simplified().left(48)).arg(text.size())
        : QStringLiteral("empty (no data offer / no owner)");
    return r;
}

ReadResult runTool(const QString &program, const QStringList &args,
                   const QByteArray &input = {})
{
    ReadResult r;
    QElapsedTimer t;
    t.start();
    const QString exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty()) {
        r.detail = QStringLiteral("%1 not installed").arg(program);
        return r;
    }
    QProcess p;
    p.start(exe, args);
    if (!p.waitForStarted(3000)) {
        r.ms = t.elapsed();
        r.detail = QStringLiteral("did not start");
        return r;
    }
    if (!input.isEmpty()) {
        p.write(input);
        p.closeWriteChannel();
    }
    p.waitForFinished(6000);
    r.ms = t.elapsed();
    const QString outText = QString::fromUtf8(p.readAllStandardOutput()).simplified();
    r.ok = p.exitCode() == 0 && !outText.isEmpty();
    r.detail = r.ok
        ? QStringLiteral("'%1'").arg(outText.left(48))
        : QStringLiteral("exit %1 out '%2' err '%3'")
              .arg(p.exitCode())
              .arg(outText.left(24),
                   QString::fromUtf8(p.readAllStandardError()).simplified().left(48));
    return r;
}

/// XAUTHORITY of the running XWayland: GNOME keeps it in a per-session file
/// that is not exported to apps, so ask the Xwayland process itself.
QString xwaylandAuth()
{
    QProcess list;
    list.start(QStringLiteral("pgrep"),
               {QStringLiteral("-u"), QString::number(::getuid()),
                QStringLiteral("-x"), QStringLiteral("Xwayland")});
    list.waitForFinished(2000);
    const QList<QByteArray> pids = list.readAllStandardOutput().simplified().split(' ');
    for (const QByteArray &pid : pids) {
        QFile env(QStringLiteral("/proc/%1/environ").arg(QString::fromLatin1(pid)));
        if (!env.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QList<QByteArray> vars = env.readAll().split('\0');
        for (const QByteArray &v : vars) {
            if (v.startsWith("XAUTHORITY=")) {
                return QString::fromLatin1(v.mid(11));
            }
        }
    }
    return qEnvironmentVariable("XAUTHORITY");
}

void ensureXEnv()
{
    if (qEnvironmentVariable("XAUTHORITY").isEmpty()) {
        const QString auth = xwaylandAuth();
        if (!auth.isEmpty()) {
            qputenv("XAUTHORITY", auth.toLatin1());
        }
    }
    if (qEnvironmentVariable("DISPLAY").isEmpty()) {
        qputenv("DISPLAY", ":0");
    }
}

#ifdef HAVE_X11
// The XWayland bridge. X11 selection semantics have no focus gate, so an X
// client can read the clipboard from the background; mutter mirrors the
// wayland clipboard into XWayland. This is the candidate clipboard backend
// for GNOME Wayland (and the fallback when Qt's wayland path stays silent).
ReadResult readX11Raw()
{
    ReadResult r;
    QElapsedTimer t;
    t.start();
    ensureXEnv();
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        r.ms = t.elapsed();
        r.detail = QStringLiteral("XOpenDisplay failed (DISPLAY=%1 - no XWayland?)")
                       .arg(qEnvironmentVariable("DISPLAY"));
        return r;
    }
    const Atom clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    const Atom targetsAtom = XInternAtom(dpy, "TARGETS", False);
    const Atom utf8Atom = XInternAtom(dpy, "UTF8_STRING", False);
    const Atom prop = XInternAtom(dpy, "CLIP_SPIKE_PROP", False);
    const Window owner = XGetSelectionOwner(dpy, clipboard);
    if (owner == None) {
        r.ms = t.elapsed();
        r.detail = QStringLiteral("no CLIPBOARD owner over XWayland");
        XCloseDisplay(dpy);
        return r;
    }
    Window w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, w, PropertyChangeMask);
    XSync(dpy, False);

    // Convert one type into our window property and return its bytes.
    auto request = [&](Atom type) -> QByteArray {
        XConvertSelection(dpy, clipboard, type, prop, w, CurrentTime);
        XSync(dpy, False);
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 1500) {
            XEvent ev;
            if (XCheckTypedWindowEvent(dpy, w, PropertyNotify, &ev)) {
                Atom gotType = None;
                int format = 0;
                unsigned long nitems = 0, after = 0;
                unsigned char *data = nullptr;
                if (XGetWindowProperty(dpy, w, prop, 0, 65536, True, AnyPropertyType,
                                       &gotType, &format, &nitems, &after,
                                       &data) == Success && data) {
                    const int bytes = int(nitems * (format / 8));
                    QByteArray result(reinterpret_cast<char *>(data), bytes);
                    XFree(data);
                    if (!result.isEmpty() || gotType != None) {
                        return result;
                    }
                }
            }
            usleep(5000);
        }
        return {};
    };

    const QByteArray targetsData = request(targetsAtom);
    QStringList targetNames;
    if (targetsData.size() >= int(sizeof(Atom))) {
        const Atom *atoms = reinterpret_cast<const Atom *>(targetsData.constData());
        const int count = targetsData.size() / int(sizeof(Atom));
        for (int i = 0; i < count; ++i) {
            if (char *name = XGetAtomName(dpy, atoms[i])) {
                targetNames << QString::fromLatin1(name);
                XFree(name);
            }
        }
    }
    QByteArray text = request(utf8Atom);
    if (text.isEmpty()) {
        text = request(XA_STRING);
    }
    XCloseDisplay(dpy);

    r.ms = t.elapsed();
    r.ok = !text.isEmpty();
    QString detail;
    if (!targetNames.isEmpty()) {
        detail += QStringLiteral("targets=[%1] ").arg(targetNames.join(QLatin1Char(' ')));
    }
    detail += r.ok
        ? QStringLiteral("'%1'").arg(QString::fromUtf8(text).simplified().left(48))
        : QStringLiteral("owner exists, conversion produced nothing");
    r.detail = detail;
    return r;
}
#endif

int runProbe(bool withXcb)
{
    QClipboard *cb = QApplication::clipboard();
    say(QStringLiteral("# clip-spike probe  platform=%1  DISPLAY=%2  XAUTHORITY=%3  wl-paste=%4\n")
            .arg(QGuiApplication::platformName(),
                 qEnvironmentVariable("DISPLAY", QStringLiteral("-")),
                 QFileInfo(qEnvironmentVariable("XAUTHORITY", QStringLiteral("-"))).fileName(),
                 QStandardPaths::findExecutable(QStringLiteral("wl-paste")).isEmpty()
                     ? QStringLiteral("missing") : QStringLiteral("found")));

    ReadResult r = readQt(cb, false);
    line(QStringLiteral("A qt-background"), r.ok, r.ms, r.detail);

    r = readQt(cb, true);
    line(QStringLiteral("B qt-window-focus"), r.ok, r.ms, r.detail);

    r = runTool(QStringLiteral("wl-paste"), {QStringLiteral("-n")});
    line(QStringLiteral("C wl-paste"), r.ok, r.ms, r.detail);

    r = runTool(QStringLiteral("wl-paste"), {QStringLiteral("-l")});
    line(QStringLiteral("C2 wl-paste -l (types)"), r.ok, r.ms, r.detail);

#ifdef HAVE_X11
    r = readX11Raw();
    line(QStringLiteral("E x11 XConvertSelection"), r.ok, r.ms, r.detail);
#else
    line(QStringLiteral("E x11 raw"), false, 0, QStringLiteral("built without X11"));
#endif

    // D: the same Qt probes inside XWayland - separate process, because the
    // Qt platform plugin is picked at startup.
    if (withXcb) {
        QProcess child;
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        e.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("xcb"));
        const QString auth = xwaylandAuth();
        if (!auth.isEmpty()) {
            e.insert(QStringLiteral("XAUTHORITY"), auth);
        }
        if (e.value(QStringLiteral("DISPLAY")).isEmpty()) {
            e.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
        }
        child.setProcessEnvironment(e);
        child.start(QApplication::applicationFilePath(), {QStringLiteral("probe")});
        child.waitForFinished(25000);
        say(QStringLiteral("# ---- child (QT_QPA_PLATFORM=xcb) ----\n"));
        say(QString::fromUtf8(child.readAllStandardOutput()));
        line(QStringLiteral("D qt-xcb (child above)"), child.exitCode() == 0, 0,
             QStringLiteral("exit %1").arg(child.exitCode()));
    }
    return 0;
}

int runSeed(const QString &text)
{
    say(QStringLiteral("# clip-spike seed \"%1\"  platform=%2\n")
            .arg(text.left(40), QGuiApplication::platformName()));
    QElapsedTimer t;
    QClipboard *cb = QApplication::clipboard();

    t.start();
    cb->setText(text);
    waitForDataChanged(cb, 200);
    line(QStringLiteral("W1 qt setText"), cb->ownsClipboard(), t.elapsed(),
         QStringLiteral("ownsClipboard=%1").arg(cb->ownsClipboard()));

    ReadResult r = runTool(QStringLiteral("wl-copy"), {}, text.toUtf8());
    line(QStringLiteral("W2 wl-copy"), r.ok, r.ms, r.detail);

    QProcess child;
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    e.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("xcb"));
    const QString auth = xwaylandAuth();
    if (!auth.isEmpty()) {
        e.insert(QStringLiteral("XAUTHORITY"), auth);
    }
    child.setProcessEnvironment(e);
    child.start(QApplication::applicationFilePath(), {QStringLiteral("seed"), text});
    child.waitForFinished(25000);
    say(QStringLiteral("# ---- child (QT_QPA_PLATFORM=xcb) ----\n"));
    say(QString::fromUtf8(child.readAllStandardOutput()));
    line(QStringLiteral("W3 qt-xcb setText"), child.exitCode() == 0, 0,
         QStringLiteral("child exit %1").arg(child.exitCode()));
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("clip-spike"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("mode"),
                                 QStringLiteral("probe | seed <text>"),
                                 QStringLiteral("[probe|seed] [text]"));
    parser.addOption({QStringLiteral("xcb"),
                      QStringLiteral("probe: also run the Qt probes in XWayland (xcb)")});
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString mode = args.value(0, QStringLiteral("probe"));
    const int rc = mode == QLatin1String("seed")
        ? runSeed(args.value(1, QStringLiteral("CLIP-SPIKE-MARKER")))
        : runProbe(parser.isSet(QStringLiteral("xcb")));
    // _exit: the xcb child crashes in Qt's platform teardown on some
    // systems (SIGSEGV after the report was already printed) - a diagnostic
    // that dies loudly is worse than one that simply leaves.
    std::fflush(stdout);
    _exit(rc);
}
