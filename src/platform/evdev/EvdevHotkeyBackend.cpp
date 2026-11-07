#include "EvdevHotkeyBackend.h"

#include <QSet>
#include <utility>
#include <QSocketNotifier>

#include <cstring>
#include <linux/input.h>
#include <unistd.h>

namespace lt {

namespace {

/// evdev keycodes -> the identifiers DoublePressDetector speaks. The
/// detector is shared with the X11 backend, which feeds it keysyms; evdev
/// codes are already layout-independent, which is what its native-code
/// path exists for.
bool mapKey(unsigned short code, quint32 &id, quint32 &nativeCode)
{
    switch (code) {
    case KEY_LEFTCTRL:
        id = KeyCtrlL; nativeCode = 0; return true;
    case KEY_RIGHTCTRL:
        id = KeyCtrlR; nativeCode = 0; return true;
    case KEY_C:
        id = KeyC; nativeCode = KeyNativeC; return true;
    default:
        return false;
    }
}

} // namespace

EvdevHotkeyBackend::EvdevHotkeyBackend(std::unique_ptr<DeviceFinder> finder,
                                       QObject *parent)
    : IHotkeyBackend(parent)
    , m_finder(std::move(finder))
{
    connect(&m_detector, &DoublePressDetector::activated, this,
            [this]() { Q_EMIT activated(); });
    m_rescanTimer.setInterval(1500);
    connect(&m_rescanTimer, &QTimer::timeout, this, [this]() { rescan(); });
}

EvdevHotkeyBackend::~EvdevHotkeyBackend()
{
    stop();
}

void EvdevHotkeyBackend::setIntervalMs(qint64 ms)
{
    m_detector.setIntervalMs(ms);
}

void EvdevHotkeyBackend::setClock(DoublePressDetector::Clock clock)
{
    m_detector.setClock(std::move(clock));
}

bool EvdevHotkeyBackend::start()
{
    m_error.clear();
    const QList<KeyboardCandidate> found = scanAndOpen();
    if (m_opened.isEmpty()) {
        m_error = describeKeyboardAccess(found);
        return false;
    }
    m_rescanTimer.start();          // keyboards may be plugged in later
    return true;
}

void EvdevHotkeyBackend::stop()
{
    m_rescanTimer.stop();
    closeAll();
}

void EvdevHotkeyBackend::closeAll()
{
    for (Opened *d : m_opened) {
        if (d->notifier) {
            d->notifier->setEnabled(false);
            delete d->notifier;
        }
        m_finder->close(d->fd);
        delete d;
    }
    m_opened.clear();
}

QList<KeyboardCandidate> EvdevHotkeyBackend::scanAndOpen()
{
    const QList<KeyboardCandidate> found = m_finder->scan();

    QSet<QString> present;
    for (const KeyboardCandidate &k : found) {
        if (k.readable && k.keyboardCapable) {
            present.insert(k.path);
        }
    }

    // Vanished devices (unplugged, or the ACL was revoked) get dropped.
    for (int i = m_opened.size() - 1; i >= 0; --i) {
        if (!present.contains(m_opened.at(i)->path)) {
            Opened *d = m_opened.takeAt(i);
            if (d->notifier) {
                d->notifier->setEnabled(false);
                delete d->notifier;
            }
            m_finder->close(d->fd);
            delete d;
        }
    }

    // New keyboards.
    for (const KeyboardCandidate &k : found) {
        if (!k.readable || !k.keyboardCapable) {
            continue;
        }
        bool already = false;
        for (Opened *d : m_opened) {
            already = already || d->path == k.path;
        }
        if (already) {
            continue;
        }
        const int fd = m_finder->open(k.path);
        if (fd < 0) {
            continue;
        }
        Opened *d = new Opened;
        d->path = k.path;
        d->fd = fd;
        d->notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(d->notifier, &QSocketNotifier::activated, this,
                [this, d]() { onDeviceReady(d); });
        m_opened << d;
    }
    return found;
}

void EvdevHotkeyBackend::rescan()
{
    scanAndOpen();
}

void EvdevHotkeyBackend::onDeviceReady(Opened *device)
{
    if (!device || !device->notifier) {
        return;
    }
    device->notifier->setEnabled(false);
    char buf[512];
    for (;;) {
        const ssize_t got = ::read(device->fd, buf, sizeof(buf));
        if (got > 0) {
            parseInto(device->pending, QByteArray(buf, int(got)));
            if (got < ssize_t(sizeof(buf))) {
                break;                       // drained
            }
            continue;
        }
        break;                               // 0 (device gone) or EAGAIN
    }
    if (device->notifier) {
        device->notifier->setEnabled(true);
    }
}

void EvdevHotkeyBackend::feedBytes(const QByteArray &bytes)
{
    parseInto(m_orphanBuffer, bytes);
}

void EvdevHotkeyBackend::parseInto(QByteArray &buffer, const QByteArray &incoming)
{
    buffer += incoming;
    constexpr int RecordSize = int(sizeof(input_event));
    while (buffer.size() >= RecordSize) {
        input_event e{};
        std::memcpy(&e, buffer.constData(), sizeof(e));
        buffer.remove(0, RecordSize);
        if (e.type != EV_KEY) {
            continue;                        // SYN, MSC, SW: noise
        }
        quint32 id = 0, native = 0;
        if (!mapKey(e.code, id, native)) {
            continue;
        }
        // value 2 (autorepeat) arrives as a press on purpose: the detector
        // collapses it, exactly as it does for X11 synthetic repeats.
        m_detector.keyEvent(e.value == 0 ? DoublePressDetector::Release
                                         : DoublePressDetector::Press,
                            id, native);
    }
}

} // namespace lt
