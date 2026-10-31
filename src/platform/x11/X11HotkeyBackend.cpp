// Qt first: X11 headers define Status/None/True/False.
#include <QDebug>

#include "X11HotkeyBackend.h"

#include <X11/Xproto.h>          // xEvent (RECORD payload)
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include <sys/select.h>
#include <unistd.h>
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
    if (m_worker) {
        return true;
    }
    Display *probe = XOpenDisplay(nullptr);
    if (!probe) {
        qInfo("llm-translate: no X display (DISPLAY=%s) - X listener disabled",
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
    m_worker = new Worker(this);
    m_worker->start();
    return true;
}

void X11HotkeyBackend::stop()
{
    if (!m_worker) {
        return;
    }
    m_stopFlag.store(true);
    m_worker->requestInterruption();
    m_worker->quit();
    m_worker->wait(3000);
    delete m_worker;
    m_worker = nullptr;
    if (m_probeDisplay) {
        XCloseDisplay(m_probeDisplay);
        m_probeDisplay = nullptr;
    }
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
// Runs on the worker thread. XI2 raw events (preferred: passive, and the
// only channel through which KWin-forwarded Wayland keystrokes reach
// XWayland) with an X RECORD fallback for displays without XInput.
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
        qInfo("llm-translate: XI2 raw key listener active on %s - "
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
        runRecord(dpy);
    }

    XCloseDisplay(dpy);
    m_threadDisplay = nullptr;
}

void X11HotkeyBackend::runRecord(Display *dpy)
{
    int major = 0, minor = 0;
    if (!XRecordQueryVersion(dpy, &major, &minor)) {
        qWarning("llm-translate: display %s has no RECORD/XInput support",
                 XDisplayString(dpy));
        return;
    }
    XRecordRange *range = XRecordAllocRange();
    if (!range) {
        return;
    }
    range->device_events.first = KeyPress;
    range->device_events.last = KeyRelease;
    XRecordClientSpec clients = XRecordAllClients;
    XRecordContext ctx = XRecordCreateContext(dpy, 0, &clients, 1, &range, 1);
    XFree(range);
    if (!ctx) {
        return;
    }
    m_recordContext = ctx;
    qInfo("llm-translate: RECORD context active (v%d.%d) - listening for "
          "double Ctrl+C",
          major, minor);
    XRecordEnableContext(dpy, ctx, &X11HotkeyBackend::recordCallback,
                         reinterpret_cast<XPointer>(this));
    XRecordFreeContext(dpy, ctx);
}

void X11HotkeyBackend::recordCallback(XPointer closure,
                                      XRecordInterceptData *data)
{
    auto *self = reinterpret_cast<X11HotkeyBackend *>(closure);
    if (!self) {
        XRecordFreeData(data);
        return;
    }
    if (self->m_stopFlag.load(std::memory_order_relaxed)
        || QThread::currentThread()->isInterruptionRequested()) {
        XRecordDisableContext(self->m_threadDisplay, self->m_recordContext);
        XRecordFreeData(data);
        return;
    }
    if (data->category == XRecordFromServer && data->data_len > 0) {
        const auto *datum = reinterpret_cast<const xEvent *>(data->data);
        const int type = datum->u.u.type;
        if (type == KeyPress || type == KeyRelease) {
            const unsigned code = datum->u.u.detail;
            self->feedKeyEvent(type, self->keysymOf(code), code);
        }
    }
    XRecordFreeData(data);
}

} // namespace lt
