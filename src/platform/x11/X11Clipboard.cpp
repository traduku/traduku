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

// ---------------------------------------------------------------------------
// X11Clipboard: the selection protocol itself
// ---------------------------------------------------------------------------
//
// Qt headers come first on purpose: Xlib defines Status, None, True and
// False, and Qt has its own meanings for three of them.

#include "core/texttargets.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QSocketNotifier>
#include <QSysInfo>

#include <sys/select.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace lt {

namespace {

QMutex g_connectMutex;    // XAUTHORITY is read from the environment at connect

QString atomName(Display *dpy, Atom a)
{
    if (a == None) {
        return {};
    }
    char *raw = XGetAtomName(dpy, a);
    const QString name = raw ? QString::fromUtf8(raw) : QString();
    if (raw) {
        XFree(raw);
    }
    return name;
}

/// Parse a TARGETS reply. Two traps, both paid for:
///
///   * `reinterpret_cast<Atom *>` on the raw buffer, which is the same bug as
///     reading fixed 4-byte words - on LP64 Xlib hands format-32 properties
///     back as `long[]`, so both read half the list and, by the luck of
///     little-endian, get the first item right. A TEXT-only owner then looked
///     like an owner with nothing to offer, and an xclip owner looked like one
///     whose only target was TARGETS.
///   * the server's byte order is not the host's. Honour ImageByteOrder, and
///     size the items the way Xlib delivered them rather than guessing 32.
///
/// A zero item is skipped rather than turned into a name: 0 is None.
QStringList atomListFromWire(const QByteArray &raw, Display *dpy)
{
    QStringList out;
    // The bytes are in the server's order; the value we hand XGetAtomName
    // must be in the host's.
    const bool serverIsBigEndian = (ImageByteOrder(dpy) == MSBFirst);
    const bool hostIsBigEndian = (QSysInfo::ByteOrder == QSysInfo::BigEndian);
    const bool swap = serverIsBigEndian != hostIsBigEndian;
    const int itemSize = static_cast<int>(sizeof(long));   // as Xlib hands it to us
    const auto *bytes = reinterpret_cast<const unsigned char *>(raw.constData());
    for (int i = 0; i + itemSize <= raw.size(); i += itemSize) {
        quint64 v = 0;
        if (swap) {
            for (int b = 0; b < itemSize; ++b) {
                v = (v << 8) | bytes[i + b];
            }
        } else {
            for (int b = itemSize - 1; b >= 0; --b) {
                v = (v << 8) | bytes[i + b];
            }
        }
        if (v == 0) {
            continue;                       // a None in the list is not a target
        }
        const QString name = atomName(dpy, static_cast<Atom>(v));
        if (!name.isEmpty()) {
            out << name;
        }
    }
    return out;
}

/// What we are willing to serve as the contents of the selection. UTF8_STRING
/// first because every modern client asks for it; TEXT and STRING because
/// legacy ones ask for those and would otherwise get an empty paste.
const char *const kServedTargets[] = {"UTF8_STRING", "TEXT", "STRING",
                                      "text/plain;charset=utf-8"};

/// Answer one SelectionRequest. `payload` is exactly what we promised when
/// we took ownership; a target we do not hold is refused by sending the
/// notify with property None, which is how ICCCM says "not available"
/// (silently not answering would leave the requester waiting forever).
void serveOne(Display *dpy, const QByteArray &payload, const XEvent &ev)
{
    const XSelectionRequestEvent *rq = &ev.xselectionrequest;
    XEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.xselection.type = SelectionNotify;
    reply.xselection.display = dpy;
    reply.xselection.requestor = rq->requestor;
    reply.xselection.selection = rq->selection;
    reply.xselection.time = rq->time;
    reply.xselection.property = None;
    reply.xselection.target = rq->target;

    const QString target = atomName(dpy, rq->target);
    if (target == QLatin1String("TARGETS")) {
        // `long` is the type XChangeProperty documents for format 32, and the
        // reply is read back through XGetWindowProperty as long[] on the other
        // side - so this is the same contract as the reader's, not a guess.
        // (The spelling matters: a narrower array here is what made our own
        // list arrive as "UTF8_STRING, <zero>, TEXT, <zero>" in
        // theTargetsListWeServeSurvivesTheWire.)
        long offered[4] = {0, 0, 0, 0};
        int n = 0;
        for (const char *t : kServedTargets) {
            offered[n++] = static_cast<long>(XInternAtom(dpy, t, False));
        }
        XChangeProperty(dpy, rq->requestor, rq->property, XA_ATOM, 32,
                        PropModeReplace, reinterpret_cast<const unsigned char *>(offered), n);
        reply.xselection.property = rq->property;
    } else if (target == QLatin1String("UTF8_STRING") || target == QLatin1String("TEXT")
               || target == QLatin1String("STRING")
               || target.startsWith(QLatin1String("text/plain"))) {
        // Keep the property type the requester asked for: a client that
        // asked for TEXT is going to read the bytes as its own locale.
        XChangeProperty(dpy, rq->requestor, rq->property, rq->target, 8,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char *>(payload.constData()),
                        payload.size());
        reply.xselection.property = rq->property;
    }

    if (reply.xselection.property != None) {
        XSendEvent(dpy, rq->requestor, False, 0, &reply);
    }
    XFlush(dpy);
}

/// Drain what is already queued, serving requests and looking for the notify
/// we are waiting for. Returns true when it saw it; `handled`, when given,
/// counts every event taken off the queue - which is what processEvents()
/// reports, because "did I get my reply" and "did I do my job as an owner"
/// are different questions.
bool drainUntilNotify(Display *dpy, Window requestor, Atom selection, Atom target,
                      bool serveOwned, const QByteArray &payload, int *handled = nullptr,
                      bool *owning = nullptr)
{
    bool got = false;
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (handled) {
            ++*handled;
        }
        if (ev.type == SelectionRequest && serveOwned) {
            serveOne(dpy, payload, ev);
            continue;
        }
        if (ev.type == SelectionClear && owning && ev.xselectionclear.selection == selection) {
            // Another client took the selection, or the server cleared it.
            // Believing we still own it means reading our own buffer instead
            // of the real clipboard, and serving text we no longer hold.
            *owning = false;
            continue;
        }
        if (ev.type == SelectionNotify
            && ev.xselection.requestor == requestor
            && ev.xselection.selection == selection
            && ev.xselection.target == target
            && ev.xselection.property != None) {
            got = true;
        }
    }
    return got;
}

} // namespace

X11Clipboard::X11Clipboard(Options options)
    : m_options(std::move(options))
{
    if (m_options.selection.isEmpty()) {
        m_options.selection = QStringLiteral("CLIPBOARD");
    }
}

X11Clipboard::~X11Clipboard()
{
    if (m_display) {
        if (m_ownWindow) {
            XDestroyWindow(m_display, m_ownWindow);
        }
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

bool X11Clipboard::openConnection() const
{
    if (m_display) {
        return true;
    }
    QString display = m_options.display;
    if (display.isEmpty()) {
        display = X11Session::resolveDisplay();
    }
    if (display.isEmpty()) {
        m_diagnostic = QStringLiteral(
            "Traduku found no X server to read the clipboard from: no DISPLAY "
            "in this session and no Xwayland running.");
        return false;
    }
    QString xauth = m_options.xauthority;
    if (xauth.isEmpty()) {
        xauth = X11Session::resolveXauthority(display);
    }

    // Xlib reads XAUTHORITY from the process environment when it connects.
    QMutexLocker lock(&g_connectMutex);
    const QByteArray previous = qgetenv("XAUTHORITY");
    if (!xauth.isEmpty()) {
        qputenv("XAUTHORITY", xauth.toLocal8Bit());
    }
    Display *dpy = XOpenDisplay(display.toLocal8Bit().constData());
    if (!dpy) {
        qputenv("XAUTHORITY", previous);       // leave the process as we found it
    }
    lock.unlock();

    if (!dpy) {
        m_diagnostic = QStringLiteral(
            "Traduku could not connect to the X server %1 (authority %2): the "
            "clipboard cannot be read or written through X selections.")
                           .arg(display, xauth.isEmpty() ? QStringLiteral("none") : xauth);
        return false;
    }
    m_display = dpy;
    m_ownWindow = XCreateSimpleWindow(m_display, RootWindow(m_display, DefaultScreen(m_display)),
                                      0, 0, 1, 1, 0, 0, 0);
    XSelectInput(m_display, m_ownWindow, SelectionNotify | StructureNotifyMask);
    XFlush(m_display);

    // Owning a selection is a promise to answer, and the application is not
    // sitting inside our read loop: watch Xlib's own socket and serve from
    // the Qt event loop. The handler drains rather than waits - a notifier
    // that blocks starves the rest of the UI.
    if (!m_notifier) {
        const int fd = ConnectionNumber(m_display);
        if (fd >= 0) {
            m_notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
            QObject::connect(m_notifier.get(), &QSocketNotifier::activated, this,
                             [this](int) {
                                 const Atom sel = XInternAtom(
                                     m_display, m_options.selection.toUtf8().constData(),
                                     False);
                                 drainUntilNotify(m_display, m_ownWindow, sel, None,
                                                  m_owning, m_payload, nullptr, &m_owning);
                             });
        }
    }
    return true;
}

bool X11Clipboard::isUsable(const Options &options)
{
    X11Clipboard probe(options);
    return probe.openConnection();
}

QString X11Clipboard::unusabilityReason(const Options &options)
{
    X11Clipboard probe(options);
    if (probe.openConnection()) {
        return {};
    }
    // connect() keeps the failure text: an Xlib refusal is only visible in
    // the return of XOpenDisplay, and losing it turns "install xauth" into an
    // afternoon of guessing.
    return probe.diagnostic();
}

int X11Clipboard::connectionFd() const
{
    return m_display ? ConnectionNumber(m_display) : -1;
}

bool X11Clipboard::processEvents(int timeoutMs) const
{
    if (!openConnection()) {
        return false;
    }
    const Atom sel = XInternAtom(m_display, m_options.selection.toUtf8().constData(), False);
    QElapsedTimer timer;
    timer.start();
    int handled = 0;
    forever {
        drainUntilNotify(m_display, m_ownWindow, sel, None, m_owning, m_payload, &handled,
                         &m_owning);
        if (timer.elapsed() >= timeoutMs) {
            break;
        }
        fd_set fds;
        FD_ZERO(&fds);
        const int fd = ConnectionNumber(m_display);
        FD_SET(fd, &fds);
        timeval tv{0, 20 * 1000};
        if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) {
            continue;                       // timeoutMs still has room
        }
    }
    return handled > 0;
}

QByteArray X11Clipboard::requestProperty(const QString &target, const QString &wantType) const
{
    const Atom sel = XInternAtom(m_display, m_options.selection.toUtf8().constData(), False);
    const Atom targetAtom = XInternAtom(m_display, target.toUtf8().constData(), False);
    // A property name of our own: reusing the target's name is what most
    // clients do, and it keeps a stale reply from being mistaken for ours.
    const Atom prop = XInternAtom(m_display, "TRADUKU_CLIPBOARD", False);

    XDeleteProperty(m_display, m_ownWindow, prop);
    XConvertSelection(m_display, sel, targetAtom, prop, m_ownWindow, CurrentTime);
    XFlush(m_display);

    QElapsedTimer timer;
    timer.start();
    while (!drainUntilNotify(m_display, m_ownWindow, sel, targetAtom, m_owning, m_payload,
                             nullptr, &m_owning)) {
        if (timer.elapsed() > 500) {
            return {};                      // the owner is not answering
        }
        fd_set fds;
        FD_ZERO(&fds);
        const int fd = ConnectionNumber(m_display);
        FD_SET(fd, &fds);
        timeval tv{0, 10 * 1000};
        select(fd + 1, &fds, nullptr, nullptr, &tv);
    }

    Atom actualType = None;
    int format = 0;
    unsigned long items = 0, bytesAfter = 0;
    unsigned char *data = nullptr;
    QByteArray out;
    if (XGetWindowProperty(m_display, m_ownWindow, prop, 0, 65536, False,
                           AnyPropertyType, &actualType, &format, &items, &bytesAfter,
                           &data) == Success
        && data) {
        // Xlib's contract: "regardless of the format, data is returned as an
        // array of long values". items * (format / 8) - 4 bytes per item for
        // format 32 - reads HALF the buffer on LP64, and the half it reads is
        // [first atom, zero]. That is the whole xclip story from the E2E grid:
        // the owner's list was fine, we truncated it to one target and a None,
        // found no text target, and reported "clipboard is empty" over a
        // clipboard the user could paste from.
        const int bytesPerItem = (format == 32)
                                     ? static_cast<int>(sizeof(long))
                                     : (format / 8);
        out = QByteArray(reinterpret_cast<const char *>(data),
                         static_cast<int>(items * bytesPerItem));
        XFree(data);
        if (!wantType.isEmpty() && atomName(m_display, actualType) != wantType) {
            return {};
        }
    }
    return out;
}

QString X11Clipboard::requestText(const QString &target) const
{
    const QByteArray bytes = requestProperty(target, QString());
    if (bytes.isEmpty()) {
        return {};
    }
    return TextTargets::decodeAtom(bytes, target);
}

QString X11Clipboard::text() const
{
    if (!openConnection()) {
        return {};
    }
    const Atom sel = XInternAtom(m_display, m_options.selection.toUtf8().constData(), False);
    const Window owner = XGetSelectionOwner(m_display, sel);
    if (owner == 0) {
        return {};                          // nothing on the clipboard
    }
    if (owner == m_ownWindow && m_owning) {
        return TextTargets::decodeAtom(m_payload,
                                      m_servedType.isEmpty() ? QStringLiteral("UTF8_STRING")
                                                             : m_servedType);
    }

    // Ask what they have, then ask for that. Two round trips, both needed:
    // asking for UTF8_STRING from a client that only offers TEXT gets an
    // empty property, not an error.
    const QByteArray targetList = requestProperty(QStringLiteral("TARGETS"),
                                                 QStringLiteral("ATOM"));
    const QStringList offered = atomListFromWire(targetList, m_display);
    const QString best = TextTargets::pickBestAtom(offered);
    if (!best.isEmpty()) {
        return requestText(best);
    }

    // No text target in the list is a reason to ask differently, not a reason
    // to say "empty". Measured against xclip on Debian 13 - the tool people
    // paste with out of terminals, tmux and scripts - whose TARGETS reply is
    //
    //     type=ATOM  format=32  nitems=2  bytes=2f 01 00 00 00 00 00 00
    //
    // one target (TARGETS itself) and a zero: xclip writes an `Atom[]` into a
    // format-32 property, Xlib takes 4 bytes per element and Atom is 8 on
    // LP64, so every target after the first is lost on the wire. `xclip -o`
    // still returns the text, so the user had a clipboard and Traduku reported
    // "clipboard is empty". ICCCM lets a client ask any target it wants: when
    // the list tells us nothing, ask the ones text owners answer.
    //
    // An image or URI selection still reads as empty - such an owner answers
    // None to these targets and the loop finds nothing.
    static const char *const kStandardTextTargets[] = {"UTF8_STRING", "STRING", "TEXT"};
    for (const char *t : kStandardTextTargets) {
        const QString text = requestText(QLatin1String(t));
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

bool X11Clipboard::setText(const QString &text)
{
    if (!openConnection()) {
        return false;
    }
    const Atom sel = XInternAtom(m_display, m_options.selection.toUtf8().constData(), False);
    m_payload = text.toUtf8();
    m_servedType = QStringLiteral("UTF8_STRING");
    XSetSelectionOwner(m_display, sel, m_ownWindow, CurrentTime);
    XSync(m_display, False);
    m_owning = XGetSelectionOwner(m_display, sel) == m_ownWindow;
    if (!m_owning) {
        m_diagnostic = QStringLiteral(
            "Traduku asked to own the %1 selection and the X server said no: "
            "the translation cannot be put on the clipboard.")
                           .arg(m_options.selection);
        m_payload.clear();
    }
    return m_owning;
}

void X11Clipboard::serveRequest(const void *requestEvent) const
{
    if (m_display && requestEvent) {
        serveOne(m_display, m_payload, *static_cast<const XEvent *>(requestEvent));
    }
}

} // namespace lt
