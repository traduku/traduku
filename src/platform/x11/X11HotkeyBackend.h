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

class QProcess;

namespace lt {

/*!
 * Passive double-Ctrl+C monitor for X11 sessions and for Wayland sessions
 * with XWayland (KWin forwards keystrokes to the X server, where XInput2
 * raw events observe them without grabbing anything - normal copying is
 * untouched).
 *
 * XWayland (mutter/gnome-shell) is different: mutter injects keystrokes
 * into the VDevice master, so XI2 raw events (slave devices only) are
 * blind, while X RECORD still sees them. libXtst's XRecordEnableContext
 * cannot be stopped from the same process (the disable request is never
 * flushed out of the blocked read loop, and the async variant does not
 * dispatch on Xwayland), so RECORD runs in the `traduku-record-keys`
 * helper process: it prints one line per key event and is killed on stop.
 *
 * All gesture logic lives in core's DoublePressDetector; this class is
 * pure I/O. feedKeyEvent() is the entry point both channels use - the test
 * feeds it directly, so the glue is covered without a live X server.
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

    /// Passive listen channel. On XWayland the raw listener is blind and
    /// RECORD is used instead. Pure so it is unit-testable.
    enum class Mode { Disabled, Xi2Raw, Record };
    static Mode chooseMode(bool xi2Available, bool isXwayland);

    /// XWayland advertises the XWAYLAND extension (modern Xwayland keeps
    /// GLX, so its presence proves nothing); WAYLAND_DISPLAY is the
    /// fallback for servers that predate the extension. Pure: unit-tested.
    static bool detectXwayland(bool hasXwaylandExtension,
                               bool hasWaylandDisplay);

    /// One line of the helper's output: "<K|R> <keysym> <keycode>".
    /// Pure so it is unit-testable.
    static bool parseRecordLine(const QByteArray &line, int *type,
                                quint32 *keysym, quint32 *nativeCode);

    /// Probe consulted when raw Ctrl presses are not delivered
    /// (native-Wayland focus syncs only modifier state). Default: XKB.
    void setModifierProbe(std::function<bool()> probe);

    /// Shared entry point of the X channels (and the unit test).
    void feedKeyEvent(int type, quint32 keysym, quint32 nativeCode);

private:
    class Worker;
    friend class Worker;
    void runWorker();               // blocking XI2 raw pump, worker thread
    void onRecordOutput();
    quint32 keysymOf(unsigned keycode) const;
    bool startRecordHelper();

    Display *m_threadDisplay = nullptr;   // worker-thread connection
    QProcess *m_proc = nullptr;           // RECORD helper process
    QString m_recordBuffer;

    Worker *m_worker = nullptr;
    Display *m_probeDisplay = nullptr;   // main-thread conn for XKB probe
    std::function<bool()> m_probe;
    QMutex m_detectorMutex;
    DoublePressDetector m_detector;
    std::atomic<bool> m_stopFlag{false};
};

} // namespace lt
