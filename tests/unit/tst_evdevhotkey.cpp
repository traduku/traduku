#include <QtTest>
#include <QSignalSpy>

#include "platform/evdev/DeviceFinder.h"
#include "platform/evdev/EvdevHotkeyBackend.h"

#include <cstring>
#include <unistd.h>
#include <linux/input.h>

using namespace lt;

namespace {

/// Hand-written device list + real fds, so no /dev/input access is needed
/// (the CI box has no uaccess rule and must not need one).
class FakeFinder : public DeviceFinder
{
public:
    QList<KeyboardCandidate> scan() override { return devices; }
    int open(const QString &path) override
    {
        ++openCalls;
        openedPaths << path;
        if (!pipeFds.isEmpty()) {
            const int fd = pipeFds.takeFirst();   // writable end stays with the test
            return fd;
        }
        return -1;
    }
    void close(int fd) override { if (fd >= 0) ::close(fd); ++closeCalls; }

    QList<KeyboardCandidate> devices;
    QList<int> pipeFds;                    // readable ends handed out by open()
    QStringList openedPaths;
    int openCalls = 0;
    int closeCalls = 0;
};

KeyboardCandidate keyboard(const QString &path, bool readable = true,
                           bool capable = true)
{
    KeyboardCandidate k;
    k.path = path;
    k.name = QStringLiteral("Fake keyboard %1").arg(path);
    k.readable = readable;
    k.keyboardCapable = capable;
    if (!readable) {
        k.error = QStringLiteral("Permission denied");
    }
    return k;
}

QByteArray ev(quint16 type, quint16 code, quint16 value)
{
    input_event e{};
    e.type = type;
    e.code = code;
    e.value = value;
    QByteArray bytes;
    bytes.resize(static_cast<int>(sizeof(e)));
    std::memcpy(bytes.data(), &e, sizeof(e));
    return bytes;
}

/// One physical Ctrl+C tap: ctrl down, C down, C up, ctrl up.
QByteArray chord()
{
    return ev(EV_KEY, KEY_LEFTCTRL, 1) + ev(EV_KEY, KEY_C, 1)
         + ev(EV_KEY, KEY_C, 0) + ev(EV_KEY, KEY_LEFTCTRL, 0)
         + ev(EV_SYN, 0, 0);
}

} // namespace

class TestEvdevHotkey : public QObject
{
    Q_OBJECT

private slots:
    void doubleChordActivates()
    {
        EvdevHotkeyBackend backend;
        backend.setIntervalMs(800);
        QSignalSpy activated(&backend, &IHotkeyBackend::activated);
        backend.feedBytes(chord());
        QCOMPARE(activated.count(), 0);            // one tap is a plain copy
        backend.feedBytes(chord());
        QCOMPARE(activated.count(), 1);
        backend.feedBytes(chord());
        QCOMPARE(activated.count(), 1);            // a third tap starts fresh
    }

    void splitAcrossReadsStillPairs()
    {
        // The kernel can hand us a partial struct; the parser must buffer.
        EvdevHotkeyBackend backend;
        QSignalSpy activated(&backend, &IHotkeyBackend::activated);
        const QByteArray both = chord() + chord();
        const int cut = static_cast<int>(sizeof(input_event)) * 3 + 2;   // mid-struct
        backend.feedBytes(both.left(cut));
        QCOMPARE(activated.count(), 0);
        backend.feedBytes(both.mid(cut));
        QCOMPARE(activated.count(), 1);
    }

    void autorepeatIsOnePress()
    {
        EvdevHotkeyBackend backend;
        QSignalSpy activated(&backend, &IHotkeyBackend::activated);
        QByteArray held = ev(EV_KEY, KEY_LEFTCTRL, 1);
        held += ev(EV_KEY, KEY_C, 1);
        for (int i = 0; i < 6; ++i) {
            held += ev(EV_KEY, KEY_C, 2);          // key repeat
        }
        held += ev(EV_KEY, KEY_C, 0) + ev(EV_KEY, KEY_LEFTCTRL, 0);
        backend.feedBytes(held);                   // one tap despite repeats
        QCOMPARE(activated.count(), 0);
        backend.feedBytes(chord());
        QCOMPARE(activated.count(), 1);
    }

    void rightControlCounts()
    {
        EvdevHotkeyBackend backend;
        QSignalSpy activated(&backend, &IHotkeyBackend::activated);
        QByteArray e = ev(EV_KEY, KEY_RIGHTCTRL, 1) + ev(EV_KEY, KEY_C, 1)
                     + ev(EV_KEY, KEY_C, 0) + ev(EV_KEY, KEY_RIGHTCTRL, 0);
        backend.feedBytes(e);
        backend.feedBytes(e);
        QCOMPARE(activated.count(), 1);
    }

    void otherKeysAndNonKeyEventsAreIgnored()
    {
        EvdevHotkeyBackend backend;
        QSignalSpy activated(&backend, &IHotkeyBackend::activated);
        QByteArray noise;
        for (int i = 0; i < 50; ++i) {
            noise += ev(EV_KEY, KEY_A, 1) + ev(EV_KEY, KEY_A, 0);
        }
        noise += ev(EV_SYN, 0, 0) + ev(EV_MSC, 4, 12345) + ev(EV_SW, 0, 1);
        backend.feedBytes(noise);
        QCOMPARE(activated.count(), 0);
    }

    void outsideTheIntervalDoesNotPair()
    {
        // The clock is injected (same seam core's detector uses), so the
        // window boundary is tested exactly instead of by sleeping.
        qint64 now = 1000;
        EvdevHotkeyBackend backend;
        backend.setClock([&now]() { return now; });
        backend.setIntervalMs(500);
        QSignalSpy activated(&backend, &IHotkeyBackend::activated);

        backend.feedBytes(chord());
        now += 900;
        backend.feedBytes(chord());
        QCOMPARE(activated.count(), 0);            // two copies, not a gesture

        now += 900;                                // a fresh pair inside the window
        backend.feedBytes(chord());
        now += 100;
        backend.feedBytes(chord());
        QCOMPARE(activated.count(), 1);
    }

    void startFailsWithActionableMessageWithoutKeyboards()
    {
        auto finder = std::make_unique<FakeFinder>();
        FakeFinder *f = finder.get();
        f->devices << keyboard(QStringLiteral("/dev/input/event3"), false);
        EvdevHotkeyBackend backend(std::move(finder));
        QVERIFY(!backend.start());
        QVERIFY2(!backend.lastError().isEmpty(), "no explanation for the operator");
        QVERIFY2(backend.lastError().contains(QStringLiteral("Permission")),
                 qPrintable(backend.lastError()));
        QVERIFY2(backend.lastError().contains(QStringLiteral("70-traduku-uaccess.rules")),
                 "must say how to fix it: " + backend.lastError().toUtf8());
    }

    void startOpensEveryReadableKeyboard()
    {
        auto finder = std::make_unique<FakeFinder>();
        FakeFinder *f = finder.get();
        f->devices << keyboard(QStringLiteral("/dev/input/event3"))
                   << keyboard(QStringLiteral("/dev/input/event4"))
                   << keyboard(QStringLiteral("/dev/input/mouse1"), true, false)
                   << keyboard(QStringLiteral("/dev/input/event9"), false);
        const int fdA = dup(STDIN_FILENO);          // any readable fd will do
        const int fdB = dup(STDIN_FILENO);
        f->pipeFds << fdA << fdB;

        EvdevHotkeyBackend backend(std::move(finder));
        QVERIFY2(backend.start(), qPrintable(backend.lastError()));
        QCOMPARE(backend.openKeyboardCount(), 2);
        const QStringList expected{QStringLiteral("/dev/input/event3"),
                                   QStringLiteral("/dev/input/event4")};
        QCOMPARE(f->openedPaths, expected);
        backend.stop();
        QCOMPARE(f->closeCalls, 2);
        QCOMPARE(backend.openKeyboardCount(), 0);
    }

    void hotplugIsPickedUpOnRescan()
    {
        auto finder = std::make_unique<FakeFinder>();
        FakeFinder *f = finder.get();
        f->devices << keyboard(QStringLiteral("/dev/input/event3"));
        f->pipeFds << dup(STDIN_FILENO);

        EvdevHotkeyBackend backend(std::move(finder));
        QVERIFY(backend.start());
        QCOMPARE(backend.openKeyboardCount(), 1);

        // someone plugs a second keyboard while Traduku runs
        f->devices << keyboard(QStringLiteral("/dev/input/event7"));
        f->pipeFds << dup(STDIN_FILENO);
        backend.rescan();
        QCOMPARE(backend.openKeyboardCount(), 2);
    }

    void unpluggedKeyboardIsDropped()
    {
        auto finder = std::make_unique<FakeFinder>();
        FakeFinder *f = finder.get();
        f->devices << keyboard(QStringLiteral("/dev/input/event3"));
        f->pipeFds << dup(STDIN_FILENO);

        EvdevHotkeyBackend backend(std::move(finder));
        QVERIFY(backend.start());
        f->devices.clear();                        // device vanished
        backend.rescan();
        QCOMPARE(backend.openKeyboardCount(), 0);
        QVERIFY(f->closeCalls >= 1);
    }

    void feedBytesFromTheOpenedDeviceActivates()
    {
        auto finder = std::make_unique<FakeFinder>();
        FakeFinder *f = finder.get();
        f->devices << keyboard(QStringLiteral("/dev/input/event3"));
        int writer = -1, reader = -1;
        int fds[2];
        QVERIFY2(pipe(fds) == 0, "pipe() failed");
        writer = fds[1];
        reader = fds[0];
        f->pipeFds << reader;

        EvdevHotkeyBackend backend(std::move(finder));
        QSignalSpy activated(&backend, &IHotkeyBackend::activated);
        QVERIFY(backend.start());
        // real write() through the fd the backend opened
        const QByteArray bytes = chord() + chord();
        QCOMPARE(::write(writer, bytes.constData(), size_t(bytes.size())),
                 qsizetype(bytes.size()));
        QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 1, 5000);
        ::close(writer);
    }

    void backendNameIsStableForBackendInfo()
    {
        EvdevHotkeyBackend backend;
        QCOMPARE(backend.backendName(), QStringLiteral("evdev"));
    }
};

QTEST_GUILESS_MAIN(TestEvdevHotkey)
#include "tst_evdevhotkey.moc"
