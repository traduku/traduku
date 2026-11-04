// evdev-spike — Stage 0 diagnostic (NOT product code; see GNOME_PLAN.md D1).
//
// Question it answers: can the app observe the literal Ctrl+C+C gesture on
// Wayland by reading /dev/input directly (no grab, no injection, copying
// untouched), and what does it cost? This is the only Wayland path that
// keeps the advertised gesture: compositors never hand foreign key presses
// to clients, but the kernel input layer is below them.
//
//   ./build/bin/evdev-spike list      # device inventory + readability
//   ./build/bin/evdev-spike listen 10 # watch 10 s, report Ctrl+C and pairs
//
// Cost of this path: the user must be able to read /dev/input/event*, i.e.
// be in group `input` (like input-remapper/keyd ask for). That is a
// keylogger-grade capability and must stay opt-in + documented.
//
// Exit codes: 0 keyboards readable; 2 permission denied on every device;
// 3 no keyboard-capable device at all.
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QString>
#include <QTextStream>
#include <QTimer>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

namespace {

void say(const QString &s)
{
    std::fputs(qPrintable(s), stdout);
    std::fflush(stdout);
}

struct Device {
    QString path;
    QString name;
    int fd = -1;
    bool hasC = false;
    bool hasCtrl = false;
    QString error;
    bool isKeyboard() const { return fd >= 0 && hasC && hasCtrl; }
};

bool hasKeyBit(int fd, unsigned short code)
{
    unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
        return false;
    }
    return (bits[code / (8 * sizeof(unsigned long))]
            & (1UL << (code % (8 * sizeof(unsigned long))))) != 0;
}

std::vector<Device> scan()
{
    std::vector<Device> devices;
    const QDir dir(QStringLiteral("/dev/input"));
    const QStringList entries =
        dir.entryList({QStringLiteral("event*")}, QDir::System, QDir::Name);
    for (const QString &entry : entries) {
        Device d;
        d.path = dir.absoluteFilePath(entry);
        d.fd = ::open(qPrintable(d.path), O_RDONLY | O_NONBLOCK);
        if (d.fd < 0) {
            d.error = QString::fromLatin1(strerror(errno));
            devices.push_back(d);
            continue;
        }
        unsigned long evBits[1] = {};
        if (ioctl(d.fd, EVIOCGBIT(0, sizeof(evBits)), evBits) == 0
            && (evBits[0] & (1UL << EV_KEY))) {
            d.hasC = hasKeyBit(d.fd, KEY_C);
            d.hasCtrl = hasKeyBit(d.fd, KEY_LEFTCTRL) || hasKeyBit(d.fd, KEY_RIGHTCTRL);
        }
        char name[128] = {};
        if (ioctl(d.fd, EVIOCGNAME(sizeof(name)), name) > 0) {
            d.name = QString::fromLocal8Bit(name);
        }
        devices.push_back(d);
    }
    return devices;
}

void reportInventory(const std::vector<Device> &devices)
{
    say(QStringLiteral("# evdev-spike  /dev/input/event*  (%1 found)\n").arg(devices.size()));
    int keyboards = 0, readable = 0, denied = 0;
    for (const Device &d : devices) {
        if (d.fd < 0) {
            if (d.error.contains(QStringLiteral("Permission"))) {
                ++denied;
            }
            say(QStringLiteral("  FAIL %1  %2\n").arg(QFileInfo(d.path).fileName().leftJustified(10), d.error));
            continue;
        }
        ++readable;
        say(QStringLiteral("  OK   %1  C=%2 ctrl=%3  '%4'\n")
                .arg(QFileInfo(d.path).fileName().leftJustified(10))
                .arg(d.hasC ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(d.hasCtrl ? QStringLiteral("yes") : QStringLiteral("no"))
                .arg(d.name.simplified().left(48)));
        if (d.isKeyboard()) {
            ++keyboards;
        }
    }
    say(QStringLiteral("summary: %1 readable, %2 keyboard-capable, %3 permission-denied\n")
            .arg(readable).arg(keyboards).arg(denied));
    if (keyboards == 0 && denied > 0) {
        // Not "usermod -aG input": the group hands keyboard reading to
        // every program of the account, forever. uaccess is the modern
        // answer (packaging/udev/70-traduku-uaccess.rules) - a logind ACL
        // for the ACTIVE seat session only. Verified matching here; the
        // ACL lands when the node is set up, so trigger MUST be add:
        // plain `udevadm trigger` sends change and does nothing.
        say(QStringLiteral(
            "hint: install the uaccess rule (NOT the 'input' group):\n"
            "  sudo install -Dm644 packaging/udev/70-traduku-uaccess.rules \\\n"
            "       /etc/udev/rules.d/70-traduku-uaccess.rules\n"
            "  sudo rm -f /etc/udev/rules.d/99-traduku-uaccess.rules\n"
            "  sudo udevadm control --reload\n"
            "  sudo udevadm trigger --action=add --subsystem-match=input\n"
            "  getfacl /dev/input/event3     # expect: user:$USER:rw-\n"
            "(the name must sort before 71-seat.rules/73-seat-late.rules -\n"
            " a 99-* rule tags too late and no ACL is ever queued - and\n"
            " --action=add is required, RUN only runs on add/move)\n"
            "one-shot for a test run, gone after reboot:\n"
            "  sudo setfacl -R -m u:$USER:rw /dev/input/event*\n"));
    }
}

/// Ctrl state machine + pair detection, mirroring core/DoublePressDetector
/// (the spike deliberately re-implements it: Stage 0 must not depend on
/// product code, and the real backend will reuse the tested detector).
struct Gesture {
    double intervalMs = 800;
    bool ctrl = false;
    QElapsedTimer sincePress;
    bool armed = false;

    bool press(unsigned short code)
    {
        if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
            ctrl = true;
            return false;
        }
        if (code != KEY_C) {
            return false;
        }
        if (!ctrl) {
            armed = false;
            return false;
        }
        if (armed && sincePress.isValid() && sincePress.elapsed() <= intervalMs) {
            armed = false;
            return true;                       // the second Ctrl+C within the window
        }
        armed = true;
        sincePress.restart();
        return false;
    }
    void release(unsigned short code)
    {
        if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
            ctrl = false;
        }
        if (code == KEY_C) {
            armed = false;   // the two presses must be separate taps
        }
    }
};

int listen(std::vector<Device> devices, int seconds)
{
    int keyboards = 0;
    for (const Device &d : devices) {
        if (d.isKeyboard()) {
            ++keyboards;
        }
    }
    if (keyboards == 0) {
        say(QStringLiteral("nothing to listen to (no readable keyboard)\n"));
        return 2;
    }
    const int ep = epoll_create1(0);
    for (const Device &d : devices) {
        if (!d.isKeyboard()) {
            continue;
        }
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = d.fd;
        epoll_ctl(ep, EPOLL_CTL_ADD, d.fd, &ev);
    }
    say(QStringLiteral("listening %1 s — press Ctrl+C twice now "
                       "(each press separately, Ctrl held)\n").arg(seconds));

    Gesture g;
    QElapsedTimer total;
    total.start();
    int gestures = 0, presses = 0;
    while (total.elapsed() < seconds * 1000) {
        epoll_event events[8];
        const int n = epoll_wait(ep, events, 8, 200);
        for (int i = 0; i < n; ++i) {
            input_event evs[64];
            const ssize_t got = ::read(events[i].data.fd, evs, sizeof(evs));
            for (ssize_t j = 0; j * long(sizeof(input_event)) < got; ++j) {
                const input_event &ev = evs[j];
                if (ev.type != EV_KEY) {
                    continue;
                }
                if (ev.value == 1) {
                    ++presses;
                    if (ev.code == KEY_C) {
                        say(QStringLiteral("  press Ctrl+C (keycode %1, fd %2, t=%3ms)\n")
                                .arg(ev.code).arg(events[i].data.fd).arg(total.elapsed()));
                    }
                    if (g.press(ev.code)) {
                        ++gestures;
                        say(QStringLiteral("  ==> GESTURE Ctrl+C+C detected (%1ms)\n")
                                .arg(total.elapsed()));
                    }
                } else if (ev.value == 0) {
                    g.release(ev.code);
                }
            }
        }
    }
    for (Device &d : devices) {
        if (d.fd >= 0) {
            ::close(d.fd);
        }
    }
    say(QStringLiteral("summary: %1 key presses, %2 gestures\n").arg(presses).arg(gestures));
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("mode"),
                                 QStringLiteral("list | listen [seconds]"),
                                 QStringLiteral("[list|listen] [seconds]"));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString mode = args.value(0, QStringLiteral("list"));
    auto devices = scan();
    reportInventory(devices);

    int keyboards = 0, denied = 0;
    for (const Device &d : devices) {
        if (d.isKeyboard()) {
            ++keyboards;
        }
        if (d.fd < 0 && d.error.contains(QStringLiteral("Permission"))) {
            ++denied;
        }
    }
    if (mode == QLatin1String("listen")) {
        return listen(std::move(devices), args.value(1, QStringLiteral("10")).toInt());
    }
    if (keyboards > 0) {
        return 0;
    }
    return denied > 0 ? 2 : 3;
}
