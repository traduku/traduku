#include "DeviceFinder.h"

#include <QDir>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace lt {

namespace {

/// KEY_MAX/64 + 1 longs; EVIOCGBIT fills the device's key bitmap.
constexpr int KeyWords = 0x2ff / 64 + 1;

bool testBit(const unsigned long *bits, int code)
{
    return ((bits[code / 64] >> (code % 64)) & 1UL) != 0;
}

} // namespace

QString keyboardAccessFixHint()
{
    return QStringLiteral(
        "grant read access with the uaccess rule: sudo install -Dm644 "
        "packaging/udev/70-traduku-uaccess.rules /etc/udev/rules.d/ and "
        "sudo udevadm trigger --action=add --subsystem-match=input (the "
        "filename must sort before 71-seat.rules; adding the user to the "
        "'input' group is the wrong fix - see packaging/README.md)");
}

namespace {

class EvdevDeviceFinder : public DeviceFinder
{
public:
    QList<KeyboardCandidate> scan() override
    {
        QList<KeyboardCandidate> found;
        QDir dir(QStringLiteral("/dev/input"));
        const QStringList entries =
            dir.entryList({QStringLiteral("event*")}, QDir::System, QDir::Name);
        for (const QString &entry : entries) {
            KeyboardCandidate k;
            k.path = dir.absoluteFilePath(entry);
            const int fd = ::open(qPrintable(k.path), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                k.error = QString::fromLatin1(strerror(errno));
                found << k;
                continue;
            }
            k.readable = true;

            unsigned long evBits[1] = {};
            // EVIOCGBIT returns the number of BYTES copied, not 0.
            if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0
                && (evBits[0] & (1UL << EV_KEY))) {
                unsigned long keyBits[KeyWords] = {};
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
                    k.keyboardCapable =
                        testBit(keyBits, KEY_C)
                        && (testBit(keyBits, KEY_LEFTCTRL) || testBit(keyBits, KEY_RIGHTCTRL));
                }
            }
            char name[128] = {};
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) > 0) {
                k.name = QString::fromLocal8Bit(name);
            }
            ::close(fd);
            found << k;
        }
        return found;
    }

    int open(const QString &path) override
    {
        return ::open(qPrintable(path), O_RDONLY | O_NONBLOCK);
    }

    void close(int fd) override
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

} // namespace

std::unique_ptr<DeviceFinder> makeEvdevDeviceFinder()
{
    return std::make_unique<EvdevDeviceFinder>();
}

QString describeKeyboardAccess(const QList<KeyboardCandidate> &found)
{
    if (found.isEmpty()) {
        return QStringLiteral("no /dev/input/event* devices at all");
    }
    int denied = 0, capable = 0;
    for (const KeyboardCandidate &k : found) {
        if (!k.readable) {
            ++denied;
        } else if (k.keyboardCapable) {
            ++capable;
        }
    }
    if (capable > 0) {
        return QString();                      // nothing to complain about
    }
    if (denied == found.size()) {
        return QStringLiteral("every input node is Permission denied for this "
                              "user - %1").arg(keyboardAccessFixHint());
    }
    if (denied > 0) {
        return QStringLiteral("%1 of %2 input nodes are Permission denied - %3")
            .arg(denied).arg(found.size()).arg(keyboardAccessFixHint());
    }
    return QStringLiteral("no device advertises KEY_C + KEY_CTRL (no keyboard "
                          "plugged in?)");
}

} // namespace lt
