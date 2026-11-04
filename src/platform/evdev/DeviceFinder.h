#pragma once

#include <QList>
#include <QString>

namespace lt {

/// One /dev/input/event* node and what the probe learned about it.
struct KeyboardCandidate
{
    QString path;
    QString name;                 // EVIOCGNAME, e.g. "Chicony USB Wireless Keyboard"
    bool readable = false;        // open(O_RDONLY) succeeded and EV_KEY is offered
    bool keyboardCapable = false; // offers KEY_C and a Ctrl
    QString error;                // strerror text when !readable
};

/*!
 * Discovers keyboards for the /dev/input gesture. An interface so the
 * backend can be tested without touching /dev (CI has no uaccess rule and
 * must not need one), and so --backend-info can reuse the same probe.
 */
class DeviceFinder
{
public:
    virtual ~DeviceFinder() = default;
    virtual QList<KeyboardCandidate> scan() = 0;
    /// O_RDONLY | O_NONBLOCK; -1 on failure. The backend owns the fd.
    virtual int open(const QString &path) = 0;
    virtual void close(int fd) = 0;
};

/// The real one (QDir /dev/input + EVIOCGBIT probes).
std::unique_ptr<DeviceFinder> makeEvdevDeviceFinder();

/// "" when the gesture can work; otherwise the problem in operator words.
/// Single source of the fix instruction (used by the backend's lastError()
/// and by --backend-info), because "Permission denied" alone never taught
/// anybody about the uaccess rule.
QString describeKeyboardAccess(const QList<KeyboardCandidate> &found);

/// The remedy sentence, shared with BackendInfo.
QString keyboardAccessFixHint();

} // namespace lt
