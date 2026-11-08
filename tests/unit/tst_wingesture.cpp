// Windows gesture backend: the low-level keyboard hook is a mechanism, the
// gesture is a decision, and only the decision is what this suite is about.
// The test writes a keyboard (WinKeyTransition values) instead of asking the
// machine for one - the same reason tst_evdevhotkey hands evdev fake
// input_event bytes instead of needing a uaccess ACL (ROADMAP.md §7.2).

#include <QtTest>
#include <QSignalSpy>

#include "platform/win/WinGesturePolicy.h"
#include "platform/win/WinHotkeyBackend.h"
#include "core/doublepressdetector.h"

using namespace lt;

namespace {

WinKeyTransition key(quint32 vk, bool pressed, bool injected = false)
{
    WinKeyTransition t;
    t.vkCode = vk;
    t.pressed = pressed;
    t.injected = injected;
    return t;
}

/// One physical Ctrl+C tap: ctrl down, C down, C up, ctrl up.
QList<WinKeyTransition> chord(quint32 ctrlVk = WinVkLControl)
{
    return {key(ctrlVk, true), key(WinVkC, true),
            key(WinVkC, false), key(ctrlVk, false)};
}

/// A hook that never touches user32: the test delivers transitions by hand
/// and can say whether the backend asked for install/teardown.
class FakeHook
{
public:
    bool install(const std::function<void(const WinKeyTransition &)> &deliver,
                 QString *error)
    {
        ++installCalls;
        if (!succeeds) {
            *error = failureText;
            return false;
        }
        m_deliver = deliver;
        return true;
    }
    void uninstall() { ++uninstallCalls; m_deliver = {}; }
    void send(const WinKeyTransition &t)
    {
        QVERIFY2(m_deliver, "the backend delivered without an installed hook");
        m_deliver(t);
    }
    void sendAll(const QList<WinKeyTransition> &ts)
    {
        for (const WinKeyTransition &t : ts) {
            send(t);
        }
    }

    bool succeeds = true;
    QString failureText = QStringLiteral("the desktop has no keyboard");
    int installCalls = 0;
    int uninstallCalls = 0;
    std::function<void(const WinKeyTransition &)> m_deliver;
};

} // namespace

class TestWinGesture : public QObject
{
    Q_OBJECT

private slots:
    // ---- the policy: VK codes in, gesture out --------------------------

    void doubleCtrlCFiresOnce()
    {
        DoublePressDetector d;
        WinGesturePolicy policy(&d);
        QSignalSpy spy(&d, &DoublePressDetector::activated);
        for (const WinKeyTransition &t : chord()) {
            policy.feed(t);
        }
        QCOMPARE(spy.count(), 0);   // one tap is not the gesture
        for (const WinKeyTransition &t : chord()) {
            policy.feed(t);
        }
        QCOMPARE(spy.count(), 1);
    }

    void cWithoutCtrlNeverFires()
    {
        DoublePressDetector d;
        WinGesturePolicy policy(&d);
        QSignalSpy spy(&d, &DoublePressDetector::activated);
        for (int i = 0; i < 20; ++i) {
            policy.feed(key(WinVkC, true));
            policy.feed(key(WinVkC, false));
        }
        QCOMPARE(spy.count(), 0);
    }

    void rightControlIsTheSameGesture()
    {
        // Physical-key matching is what makes a Cyrillic layout irrelevant;
        // the same must be true of the right Ctrl.
        DoublePressDetector d;
        WinGesturePolicy policy(&d);
        QSignalSpy spy(&d, &DoublePressDetector::activated);
        for (const WinKeyTransition &t : chord(WinVkRControl)) {
            policy.feed(t);
        }
        for (const WinKeyTransition &t : chord(WinVkRControl)) {
            policy.feed(t);
        }
        QCOMPARE(spy.count(), 1);
    }

    void vkControlBothVariantsAreAccepted()
    {
        // Some drivers report VK_CONTROL (0x11) rather than left/right.
        DoublePressDetector d;
        WinGesturePolicy policy(&d);
        QSignalSpy spy(&d, &DoublePressDetector::activated);
        for (const WinKeyTransition &t : chord(WinVkControl)) {
            policy.feed(t);
        }
        for (const WinKeyTransition &t : chord(WinVkControl)) {
            policy.feed(t);
        }
        QCOMPARE(spy.count(), 1);
    }

    void autoRepeatCollapsesIntoOnePress()
    {
        // Holding Ctrl+C produces a stream of C-down without a release. Two
        // presses must mean two *taps*, or holding the key fires the gesture.
        DoublePressDetector d;
        WinGesturePolicy policy(&d);
        QSignalSpy spy(&d, &DoublePressDetector::activated);
        policy.feed(key(WinVkLControl, true));
        for (int i = 0; i < 10; ++i) {
            policy.feed(key(WinVkC, true));
        }
        policy.feed(key(WinVkC, false));
        policy.feed(key(WinVkLControl, false));
        QCOMPARE(spy.count(), 0);
    }

    void thirdPressInABurstIsIgnored()
    {
        DoublePressDetector d;
        WinGesturePolicy policy(&d);
        QSignalSpy spy(&d, &DoublePressDetector::activated);
        for (int i = 0; i < 3; ++i) {
            for (const WinKeyTransition &t : chord()) {
                policy.feed(t);
            }
        }
        QCOMPARE(spy.count(), 1);
    }

    void injectedEventsAreCountedSeparately()
    {
        // A test injects keys; a user does not. --backend-info must be able to
        // tell the two apart, or "the gesture works" could mean "my own test
        // works" on a machine where no real key was ever seen.
        DoublePressDetector d;
        WinGesturePolicy policy(&d);
        policy.feed(key(WinVkLControl, true, true));
        policy.feed(key(WinVkLControl, false, true));
        policy.feed(key(WinVkC, true, false));
        policy.feed(key(WinVkC, false, false));
        QCOMPARE(policy.injectedCount(), quint64(2));
        QCOMPARE(policy.realCount(), quint64(2));
    }

    // ---- the backend: install, queue, stop -----------------------------

    void startInstallsTheHookAndReportsIt()
    {
        FakeHook hook;
        WinHotkeyBackend backend(
            [h = &hook](auto &&d, auto &&e) { return h->install(d, e); },
            [h = &hook]() { h->uninstall(); });
        QVERIFY2(backend.start(), qPrintable(backend.lastError()));
        QCOMPARE(hook.installCalls, 1);
        QCOMPARE(backend.backendName(), QStringLiteral("win-ll-hook"));
        backend.stop();
        QCOMPARE(hook.uninstallCalls, 1);
    }

    void startFailsWithTheReasonTheHookGave()
    {
        FakeHook hook;
        hook.succeeds = false;
        hook.failureText = QStringLiteral("no interactive desktop (session 0)");
        WinHotkeyBackend backend(
            [h = &hook](auto &&d, auto &&e) { return h->install(d, e); },
            [h = &hook]() { h->uninstall(); });
        QVERIFY(!backend.start());
        QCOMPARE(backend.lastError(), hook.failureText);
        QCOMPARE(hook.uninstallCalls, 0);   // nothing to tear down
    }

    void transitionsAfterStartFireThroughTheEventLoop()
    {
        FakeHook hook;
        WinHotkeyBackend backend(
            [h = &hook](auto &&d, auto &&e) { return h->install(d, e); },
            [h = &hook]() { h->uninstall(); });
        QVERIFY(backend.start());
        QSignalSpy spy(&backend, &IHotkeyBackend::activated);
        // The hook callback runs while the system is waiting on us, so the
        // backend must hand the work to the event loop instead of translating
        // inside the callback. Queued: nothing may have fired yet.
        hook.sendAll(chord());
        hook.sendAll(chord());
        QCOMPARE(spy.count(), 0);
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);
        backend.stop();
    }

    void stopSilencesTheGesture()
    {
        FakeHook hook;
        WinHotkeyBackend backend(
            [h = &hook](auto &&d, auto &&e) { return h->install(d, e); },
            [h = &hook]() { h->uninstall(); });
        QVERIFY(backend.start());
        QSignalSpy spy(&backend, &IHotkeyBackend::activated);
        backend.stop();
        hook.m_deliver = {};              // a stopped backend must not deliver
        for (int i = 0; i < 4; ++i) {
            for (const WinKeyTransition &t : chord()) {
                backend.onTransition(t);   // as if the hook still called in
            }
        }
        QVERIFY(!spy.wait(200));
        QCOMPARE(spy.count(), 0);
    }

    void intervalComesFromSettingsNotFromAHardcodedGuess()
    {
        FakeHook hook;
        WinHotkeyBackend backend(
            [h = &hook](auto &&d, auto &&e) { return h->install(d, e); },
            [h = &hook]() { h->uninstall(); });
        backend.setIntervalMs(250);
        QVERIFY(backend.start());
        QCOMPARE(backend.intervalMs(), qint64(250));
        backend.stop();
    }
};

QTEST_GUILESS_MAIN(TestWinGesture)
#include "tst_wingesture.moc"
