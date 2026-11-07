// evdev-spike — Stage 0 diagnostic (NOT product code; see ROADMAP.md §2).
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
// Cost of this path: the user must be able to read /dev/input/event*.
// Grant it with packaging/udev/70-traduku-uaccess.rules (uaccess: an ACL
// for the ACTIVE seat session only), NOT with the `input` group - see
// packaging/README.md for why, including the two traps (rule file must sort
// before 71-seat.rules, trigger needs --action=add).
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

#include "core/doublepressdetector.h"

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
        // NOTE EVIOCGBIT returns the number of bytes copied (8/96), NOT 0.
        // Comparing with == 0 silently disabled key detection for every
        // device - the spike then reported "0 keyboard-capable" on a box
        // with a perfectly readable keyboard.
        if (ioctl(d.fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0
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

/// evdev keycode -> the identifiers DoublePressDetector is configured with.
/// The detector is shared with the X11 backend and unit-tested
/// (tests/unit/tst_doublepressdetector.cpp); Stage 3's EvdevHotkeyBackend
/// feeds it the same way, so the spike drives THE REAL algorithm instead
/// of a throwaway copy - which matters: the first copy of this file had a
/// state machine that could never fire (it cleared the pending press on the
/// C release, and the two taps always release C in between).
bool mapKey(unsigned short code, quint32 &id, quint32 &nativeCode)
{
    switch (code) {
    case KEY_LEFTCTRL:
        id = lt::KeyCtrlL; nativeCode = 0; return true;
    case KEY_RIGHTCTRL:
        id = lt::KeyCtrlR; nativeCode = 0; return true;
    case KEY_C:
        // evdev codes are already layout-independent, which is what the
        // detector's native-code path exists for.
        id = lt::KeyC; nativeCode = lt::KeyNativeC; return true;
    default:
        return false;
    }
}

/// Feed scripted events through the real detector with a controllable
/// clock - proves the gesture logic without anyone's fingers (the kernel
/// path is covered by `listen`, the VM grid uses QMP send-keys).
int selftest()
{
    int failures = 0;
    struct Script {
        const char *name;
        int expectedFires;
        // (evdev code, value) pairs; value 0 = release, 1 = press,
        // 2 = autorepeat (delivered as a press, the detector must collapse)
        std::vector<std::pair<unsigned short, int>> events;
        std::vector<qint64> gaps;          // ms advanced after each event
    };
    const auto chord = [](std::vector<std::pair<unsigned short, int>> &ev,
                          std::vector<qint64> &gaps, qint64 beforeChord) {
        if (beforeChord) {
            gaps.push_back(beforeChord);
            ev.push_back({0, 0});                 // time-only step
        }
        ev.insert(ev.end(), {{KEY_LEFTCTRL, 1}, {KEY_C, 1}, {KEY_C, 0}, {KEY_LEFTCTRL, 0}});
        for (int i = 0; i < 4; ++i) {
            gaps.push_back(20);
        }
    };

    std::vector<Script> scripts;
    {
        Script s{ "single chord does not fire", 0, {}, {} };
        chord(s.events, s.gaps, 0);
        scripts.push_back(s);
    }
    {
        Script s{ "two chords = one gesture", 1, {}, {} };
        chord(s.events, s.gaps, 0);
        chord(s.events, s.gaps, 200);
        scripts.push_back(s);
    }
    {
        Script s{ "chords outside the 500 ms window do not fire", 0, {}, {} };
        chord(s.events, s.gaps, 0);
        chord(s.events, s.gaps, 900);
        scripts.push_back(s);
    }
    {
        Script s{ "right ctrl also works", 1, {}, {} };
        chord(s.events, s.gaps, 0);
        chord(s.events, s.gaps, 200);
        // the second chord is held with the RIGHT ctrl
        s.events[s.events.size() - 4] = { KEY_RIGHTCTRL, 1 };
        s.events[s.events.size() - 1] = { KEY_RIGHTCTRL, 0 };
        scripts.push_back(s);
    }
    {
        Script s{ "typing c without ctrl never fires", 0, {}, {} };
        for (int i = 0; i < 4; ++i) {
            s.events.push_back({ KEY_C, 1 });
            s.gaps.push_back(30);
            s.events.push_back({ KEY_C, 0 });
            s.gaps.push_back(30);
        }
        scripts.push_back(s);
    }
    {
        Script s{ "ctrl alone never fires", 0, {}, {} };
        for (int i = 0; i < 3; ++i) {
            s.events.push_back({ KEY_LEFTCTRL, 1 });
            s.gaps.push_back(40);
            s.events.push_back({ KEY_LEFTCTRL, 0 });
            s.gaps.push_back(40);
        }
        scripts.push_back(s);
    }
    {
        // held Ctrl, C hammered by key repeat, released, repeated once more
        Script s{ "auto-repeat collapses into one press", 1, {}, {} };
        s.events.push_back({ KEY_LEFTCTRL, 1 });
        s.gaps.push_back(20);
        for (int round = 0; round < 2; ++round) {
            s.events.push_back({ KEY_C, 1 });
            s.gaps.push_back(15);
            for (int r = 0; r < 5; ++r) {
                s.events.push_back({ KEY_C, 2 });
                s.gaps.push_back(30);
            }
            s.events.push_back({ KEY_C, 0 });
            s.gaps.push_back(round == 0 ? 150 : 20);
        }
        s.events.push_back({ KEY_LEFTCTRL, 0 });
        s.gaps.push_back(20);
        scripts.push_back(s);
    }

    for (Script &s : scripts) {
        qint64 now = 1000;
        int fires = 0;
        lt::DoublePressDetector det;
        det.setIntervalMs(500);
        det.setClock([&now]() { return now; });
        QObject::connect(&det, &lt::DoublePressDetector::activated,
                         [&fires]() { ++fires; });
        for (size_t i = 0; i < s.events.size(); ++i) {
            const auto [code, value] = s.events[i];
            if (code != 0) {
                quint32 id = 0, native = 0;
                if (mapKey(code, id, native)) {
                    det.keyEvent(value == 0 ? lt::DoublePressDetector::Release
                                            : lt::DoublePressDetector::Press,
                                 id, native);
                }
            }
            now += i < s.gaps.size() ? s.gaps[i] : 0;
        }
        const bool ok = (fires == s.expectedFires);
        if (!ok) {
            ++failures;
        }
        say(QStringLiteral("  %1 %2 (expected %3 gesture(s), got %4)\n")
                .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                     QString::fromUtf8(s.name).leftJustified(42))
                .arg(s.expectedFires).arg(fires));
    }
    say(QStringLiteral("selftest: %1 script(s), %2 failed\n")
            .arg(scripts.size()).arg(failures));
    return failures == 0 ? 0 : 1;
}

int listen(std::vector<Device> devices, int seconds)
{
    int keyboards = 0, readable = 0;
    for (const Device &d : devices) {
        if (d.fd >= 0) {
            ++readable;
        }
        if (d.isKeyboard()) {
            ++keyboards;
        }
    }
    if (keyboards == 0) {
        // Distinguish the two failures - last time this printed "no
        // readable keyboard" while every device WAS readable and the real
        // cause was a broken capability check inside this very tool.
        say(readable > 0
                ? QStringLiteral("no device advertises KEY_C + KEY_CTRL although "
                                 "%1 are readable - the keyboard is missing, or a "
                                 "capability check here is wrong (see EVIOCGBIT "
                                 "returning byte count, not 0)\n").arg(readable)
                : QStringLiteral("nothing to listen to (no readable keyboard)\n"));
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

    // THE product detector - Stage 3 wires this same object to these same
    // events, so what `listen` proves here is the shipping logic.
    lt::DoublePressDetector det;
    det.setIntervalMs(800);
    int gestures = 0;
    QElapsedTimer total;
    total.start();
    QObject::connect(&det, &lt::DoublePressDetector::activated, [&]() {
        ++gestures;
        say(QStringLiteral("  ==> GESTURE Ctrl+C+C detected (%1ms)\n").arg(total.elapsed()));
    });
    int presses = 0;
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
                quint32 id = 0, native = 0;
                if (!mapKey(ev.code, id, native)) {
                    continue;
                }
                if (ev.value != 0) {
                    ++presses;
                    if (ev.code == KEY_C) {
                        say(QStringLiteral("  press Ctrl+C (keycode %1, fd %2, t=%3ms)\n")
                                .arg(ev.code).arg(events[i].data.fd).arg(total.elapsed()));
                    }
                }
                // value 2 (autorepeat) is fed as a press on purpose: the
                // detector must collapse it, exactly as it does for X11.
                det.keyEvent(ev.value == 0 ? lt::DoublePressDetector::Release
                                           : lt::DoublePressDetector::Press,
                             id, native);
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
                                 QStringLiteral("list | listen [seconds] | selftest"),
                                 QStringLiteral("[list|listen|selftest] [seconds]"));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString mode = args.value(0, QStringLiteral("list"));

    // no device access needed, runs anywhere
    if (mode == QLatin1String("selftest")) {
        return selftest();
    }

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
