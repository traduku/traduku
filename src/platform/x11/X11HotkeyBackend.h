#pragma once

#include "core/doublepressdetector.h"
#include "interfaces/ihotkeybackend.h"

#include <QMutex>
#include <QThread>
#include <atomic>

// NOTE: X11 headers define Status/None/True/False - consumers must include
// Qt headers BEFORE this one (see X11HotkeyBackend.cpp include order).
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/record.h>

namespace lt {

/*!
 * Passive double-Ctrl+C monitor for X11 sessions and for Wayland sessions
 * with XWayland (KWin forwards keystrokes to the X server, where XInput2
 * raw events observe them without grabbing anything - normal copying is
 * untouched). Falls back to the X RECORD extension on ancient displays.
 *
 * All gesture logic lives in core's DoublePressDetector; this class is
 * pure I/O: open display, select events, pump, translate to key events.
 * feedKeyEvent() is the same entry point the X loops use - the test feeds
 * it directly, so the glue is covered without a live X server.
 */
class X11HotkeyBackend : public IHotkeyBackend
{
    Q_OBJECT
public:
    explicit X11HotkeyBackend(QObject *parent = nullptr);
    ~X11HotkeyBackend() override;

    /// false = no display available (pure Wayland session): caller falls
    /// back to the global shortcut.
    bool start() override;
    void stop() override;
    QString backendName() const override
    {
        return QStringLiteral("x11-xi2");
    }

    void setIntervalMs(qint64 ms);

    /// Probe consulted when raw Ctrl presses are not delivered
    /// (native-Wayland focus syncs only modifier state). Default: XKB.
    void setModifierProbe(std::function<bool()> probe);

    /// Shared entry point of the X loops (and the unit test).
    void feedKeyEvent(int type, quint32 keysym, quint32 nativeCode);

private:
    class Worker;
    friend class Worker;
    void runWorker();               // blocking X pump, worker thread
    void runRecord(Display *dpy);
    static void recordCallback(XPointer closure, XRecordInterceptData *data);
    quint32 keysymOf(unsigned keycode) const;

    Display *m_threadDisplay = nullptr;   // worker-thread connection
    XRecordContext m_recordContext = 0;

    Worker *m_worker = nullptr;
    Display *m_probeDisplay = nullptr;   // main-thread conn for XKB probe
    std::function<bool()> m_probe;
    QMutex m_detectorMutex;
    DoublePressDetector m_detector;
    std::atomic<bool> m_stopFlag{false};
};

} // namespace lt
