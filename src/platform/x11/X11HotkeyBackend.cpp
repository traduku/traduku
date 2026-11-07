// Qt first: X11 headers define Status/None/True/False.
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "X11HotkeyBackend.h"

#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include <sys/select.h>
#include <cerrno>

namespace lt {

// ---------------------------------------------------------------- worker
class X11HotkeyBackend::Worker : public QThread
{
public:
    explicit Worker(X11HotkeyBackend *backend) : m_b(backend) {}

protected:
    void run() override { m_b->runWorker(); }

private:
    X11HotkeyBackend *m_b;
};

// --------------------------------------------------------------- backend
X11HotkeyBackend::X11HotkeyBackend(QObject *parent)
    : IHotkeyBackend(parent)
{
    connect(&m_detector, &DoublePressDetector::activated,
            this, &IHotkeyBackend::activated);
}

X11HotkeyBackend::~X11HotkeyBackend()
{
    stop();
}

X11HotkeyBackend::Mode X11HotkeyBackend::chooseMode(bool xi2Available,
                                                    bool isXwayland)
{
    if (!xi2Available) {
        return Mode::Record;
    }
    // Mutter injects Wayland keystrokes into the VDevice master; XI2
    // RawEvents fire for slave devices only, so on XWayland the raw
    // listener is blind - RECORD sees injected core events.
    return isXwayland ? Mode::Record : Mode::Xi2Raw;
}

bool X11HotkeyBackend::detectXwayland(bool hasXwaylandExtension,
                                      bool hasWaylandDisplay)
{
    return hasXwaylandExtension || hasWaylandDisplay;
}

bool X11HotkeyBackend::parseRecordLine(const QByteArray &line, int *type,
                                       quint32 *keysym, quint32 *nativeCode)
{
    const QList<QByteArray> parts = line.trimmed().split(' ');
    if (parts.size() != 3 || (parts[0] != "K" && parts[0] != "R")) {
        return false;
    }
    bool okSym = false, okCode = false;
    const uint sym = parts[1].toUInt(&okSym);
    const uint code = parts[2].toUInt(&okCode);
    if (!okSym || !okCode) {
        return false;
    }
    *type = parts[0] == "K" ? KeyPress : KeyRelease;
    *keysym = sym;
    *nativeCode = code;
    return true;
}

void X11HotkeyBackend::setIntervalMs(qint64 ms)
{
    QMutexLocker lock(&m_detectorMutex);
    m_detector.setIntervalMs(ms);
}

void X11HotkeyBackend::setModifierProbe(std::function<bool()> probe)
{
    m_probe = std::move(probe);
}

bool X11HotkeyBackend::start()
{
    if (m_worker || m_proc) {
        return true;
    }
    Display *probe = XOpenDisplay(nullptr);
    if (!probe) {
        qInfo("traduku: no X display (DISPLAY=%s) - X listener disabled",
              qPrintable(QString::fromLocal8Bit(qgetenv("DISPLAY"))));
        return false;
    }
    m_probeDisplay = probe;
    if (!m_probe) {
        m_probe = [this]() {
            if (!m_probeDisplay) {
                return false;
            }
            XkbStateRec st;
            if (XkbGetState(m_probeDisplay, XkbUseCoreKbd, &st) != Success) {
                return false;
            }
            return ((st.base_mods | st.latched_mods | st.locked_mods)
                    & ControlMask) != 0;
        };
    }

    m_stopFlag.store(false);
    {
        QMutexLocker lock(&m_detectorMutex);
        m_detector.setModifierProbe(m_probe);
    }

    int xiOp = 0, xiEv = 0, xiErr = 0;
    int xiMajor = 2, xiMinor = 1;
    const bool xi2Ok =
        XQueryExtension(probe, "XInputExtension", &xiOp, &xiEv, &xiErr)
        && XIQueryVersion(probe, &xiMajor, &xiMinor) == Success;
    // Xwayland ships GLX, so probe its own extension instead of guessing
    // from the absence of GLX.
    int xwOp = 0, xwEv = 0, xwErr = 0;
    const bool isXwayland =
        detectXwayland(XQueryExtension(probe, "XWAYLAND", &xwOp, &xwEv, &xwErr),
                       !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"));

    switch (chooseMode(xi2Ok, isXwayland)) {
    case Mode::Xi2Raw:
        m_worker = new Worker(this);
        m_worker->start();
        return true;
    case Mode::Record:
        if (startRecordHelper()) {
            return true;
        }
        qWarning("traduku: %s not found - X key listener disabled "
                 "(install the package's helper)",
                 qPrintable(QStringLiteral("traduku-record-keys")));
        break;
    case Mode::Disabled:
        break;
    }
    stop();
    return false;
}

bool X11HotkeyBackend::startRecordHelper()
{
    // libXtst's blocking RECORD cannot be stopped from this process and its
    // async variant does not dispatch on Xwayland, so RECORD runs in a child
    // that prints key events and is killed on stop().
    QString helper = qEnvironmentVariable("TRADUKU_RECORD_HELPER");
    if (helper.isEmpty()) {
        const QString dir = QCoreApplication::applicationDirPath();
        const QStringList candidates{
            dir + QStringLiteral("/traduku-record-keys"),
            dir + QStringLiteral("/../bin/traduku-record-keys"),
        };
        for (const QString &c : candidates) {
            if (QFileInfo(c).isExecutable()) {
                helper = c;
                break;
            }
        }
    }
    if (helper.isEmpty()) {
        helper = QStandardPaths::findExecutable(
            QStringLiteral("traduku-record-keys"));
    }
    if (helper.isEmpty()) {
        return false;
    }

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this,
            &X11HotkeyBackend::onRecordOutput);
    m_proc->start(helper, {});
    if (!m_proc->waitForStarted(2000)) {
        delete m_proc;
        m_proc = nullptr;
        return false;
    }
    qInfo("traduku: X RECORD key listener active on %s (helper pid %lld) - "
          "listening for double Ctrl+C",
          qPrintable(QString::fromLocal8Bit(qgetenv("DISPLAY"))),
          static_cast<long long>(m_proc->processId()));
    return true;
}

void X11HotkeyBackend::onRecordOutput()
{
    m_recordBuffer += QString::fromLatin1(m_proc->readAllStandardOutput());
    int nl = -1;
    while ((nl = m_recordBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QByteArray line = m_recordBuffer.left(nl).toLatin1();
        m_recordBuffer.remove(0, nl + 1);
        int type = 0;
        quint32 sym = 0, code = 0;
        if (parseRecordLine(line, &type, &sym, &code)) {
            feedKeyEvent(type, sym, code);
        }
    }
}

void X11HotkeyBackend::stop()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
        delete m_proc;
        m_proc = nullptr;
    }
    if (m_worker) {
        m_stopFlag.store(true);
        m_worker->requestInterruption();
        m_worker->quit();
        m_worker->wait(3000);
        delete m_worker;
        m_worker = nullptr;
    }
    if (m_probeDisplay) {
        XCloseDisplay(m_probeDisplay);
        m_probeDisplay = nullptr;
    }
    m_recordBuffer.clear();
}

void X11HotkeyBackend::feedKeyEvent(int type, quint32 keysym, quint32 nativeCode)
{
    QMutexLocker lock(&m_detectorMutex);
    m_detector.keyEvent(type == KeyPress ? DoublePressDetector::Press
                                         : DoublePressDetector::Release,
                        keysym, nativeCode);
}

quint32 X11HotkeyBackend::keysymOf(unsigned keycode) const
{
    return static_cast<quint32>(XKeycodeToKeysym(m_threadDisplay, keycode, 0));
}

// -------------------------------------------------------------- X loops
// Runs on the worker thread (native Xorg only: XWayland uses the RECORD
// helper). XI2 raw events are passive and see KWin-forwarded keystrokes.
void X11HotkeyBackend::runWorker()
{
    Display *dpy = XOpenDisplay(nullptr);   // per-thread connection
    if (!dpy) {
        return;
    }
    m_threadDisplay = dpy;

    int xiOpcode = 0, xiEvent = 0, xiError = 0;
    int xiMajor = 2, xiMinor = 1;
    if (XQueryExtension(dpy, "XInputExtension", &xiOpcode, &xiEvent, &xiError)
        && XIQueryVersion(dpy, &xiMajor, &xiMinor) == Success) {
        Window root = RootWindow(dpy, DefaultScreen(dpy));
        XIEventMask mask;
        unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {0};
        mask.deviceid = XIAllMasterDevices;
        mask.mask_len = sizeof(bits);
        mask.mask = bits;
        XISetMask(bits, XI_RawKeyPress);
        XISetMask(bits, XI_RawKeyRelease);
        XISelectEvents(dpy, root, &mask, 1);
        XSync(dpy, 0);
        qInfo("traduku: XI2 raw key listener active on %s - "
              "listening for double Ctrl+C",
              XDisplayString(dpy));

        const int xfd = ConnectionNumber(dpy);
        while (!m_stopFlag.load(std::memory_order_relaxed)
               && !QThread::currentThread()->isInterruptionRequested()) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xfd, &fds);
            timeval tv{0, 200000};
            const int r = select(xfd + 1, &fds, nullptr, nullptr, &tv);
            if (r < 0 && errno == EINTR) {
                continue;
            }
            while (XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                if (ev.type != GenericEvent || ev.xcookie.extension != xiOpcode
                    || !XGetEventData(dpy, &ev.xcookie)) {
                    continue;
                }
                if (ev.xcookie.evtype == XI_RawKeyPress
                    || ev.xcookie.evtype == XI_RawKeyRelease) {
                    const auto *raw = static_cast<const XIRawEvent *>(ev.xcookie.data);
                    feedKeyEvent(ev.xcookie.evtype == XI_RawKeyPress ? KeyPress
                                                                     : KeyRelease,
                                 keysymOf(raw->detail), raw->detail);
                }
                XFreeEventData(dpy, &ev.xcookie);
            }
        }
    } else {
        qWarning("traduku: display %s has no XInput2 support",
                 XDisplayString(dpy));
    }

    XCloseDisplay(dpy);
    m_threadDisplay = nullptr;
}

} // namespace lt
