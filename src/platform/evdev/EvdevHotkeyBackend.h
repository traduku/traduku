#pragma once

#include "core/doublepressdetector.h"
#include "interfaces/ihotkeybackend.h"
#include "platform/evdev/DeviceFinder.h"

#include <QObject>
#include <QTimer>

#include <memory>

class QSocketNotifier;

namespace lt {

/*!
 * The literal Ctrl+C+C gesture on Wayland (GNOME_PLAN.md D1).
 *
 * A Wayland compositor never hands foreign keystrokes to a client, and the
 * advertised gesture is literally "Ctrl+C twice", so the only channel left
 * is the kernel input layer: read /dev/input/event* (no EVIOCGRAB, no
 * injection - every other reader, including the compositor, still gets its
 * own copy, so normal copying is untouched).
 *
 * All gesture logic lives in core's DoublePressDetector (the same object the
 * X11 backend feeds); this class is pure I/O:
 *   feedBytes()  - the test seam and the parser the read path uses;
 *   rescan()     - hot-plug: keyboards may appear after startup;
 *   lastError()  - actionable text when no keyboard is readable.
 *
 * Cost: reading /dev/input needs an ACL (packaging/udev/70-traduku-uaccess
 * .rules). Without it start() fails and the caller falls back to the
 * gsettings global shortcut.
 */
class EvdevHotkeyBackend : public IHotkeyBackend
{
    Q_OBJECT
public:
    explicit EvdevHotkeyBackend(std::unique_ptr<DeviceFinder> finder =
                                    makeEvdevDeviceFinder(),
                                QObject *parent = nullptr);
    ~EvdevHotkeyBackend() override;

    /// false = no readable keyboard; see lastError() for what to do about it.
    bool start() override;
    void stop() override;
    QString backendName() const override { return QStringLiteral("evdev"); }

    /// Two taps further apart than this are two separate copies.
    void setIntervalMs(qint64 ms);
    qint64 intervalMs() const { return m_detector.intervalMs(); }

    /// Test seam (same injectable clock core's detector exposes), so the
    /// window boundary is tested without sleeping.
    void setClock(DoublePressDetector::Clock clock);

    QString lastError() const { return m_error; }
    int openKeyboardCount() const { return m_opened.size(); }

    /// Pick up keyboards that appeared/vanished (also called on a timer).
    void rescan();

    /// Raw bytes exactly as read() returns them: whole input_event structs
    /// plus possibly a partial tail. Unit tests drive the gesture here.
    void feedBytes(const QByteArray &bytes);

private:
    struct Opened
    {
        QString path;
        int fd = -1;
        QSocketNotifier *notifier = nullptr;
        QByteArray pending;            // partial input_event from the last read
    };

    void closeAll();
    /// Opens every readable+capable device not already open. Returns the
    /// candidate list the finder reported (for error text).
    QList<KeyboardCandidate> scanAndOpen();
    void parseInto(QByteArray &buffer, const QByteArray &incoming);
    void onDeviceReady(Opened *device);

    std::unique_ptr<DeviceFinder> m_finder;
    DoublePressDetector m_detector;
    QList<Opened *> m_opened;
    QByteArray m_orphanBuffer;         // feedBytes() without a device
    QTimer m_rescanTimer;
    QString m_error;
};

} // namespace lt
