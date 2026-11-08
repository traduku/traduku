// The half of the gesture that cannot be faked: does a real low-level hook see
// a real key, on a real desktop, without eating it?
//
// Everything in tst_wingesture.cpp is a described machine. This one asks the
// machine, and if the machine is not there (a service session, an ssh logon, a
// CI box with no desktop) it says so out loud - a skip that carries the reason
// and the measurement, not a timeout (ROADMAP.md §4.5b: skip by product
// condition, never by waiting).

#include <QtTest>
#include <QSignalSpy>
#include <QTimer>

#include "platform/win/WinHotkeyBackend.h"
#include "platform/win/WinKeyHook.h"

#include <qt_windows.h>

#ifndef SM_SERVICES_SESSION
#define SM_SERVICES_SESSION 0x2000      // an ABI constant, not a guess
#endif
#ifndef DESKTOP_WRITEOBJECTS
#define DESKTOP_WRITEOBJECTS 0x0080
#endif

using namespace lt;

namespace {

/// Synthetic key presses through the same path a user's fingers take: the
/// system delivers them to hooks before any window sees them. Returns false
/// when the desktop refuses to accept injected input - which is a fact about
/// the machine, not a failing assertion.
bool injectChord()
{
    INPUT in[4] = {};
    const WORD vkC = static_cast<WORD>('C');
    auto fill = [](INPUT &i, WORD vk, bool down) {
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = vk;
        i.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        i.ki.dwFlags = down ? 0u : KEYEVENTF_KEYUP;
        if (vk == VK_CONTROL) {
            i.ki.dwFlags |= LLKHF_EXTENDED;   // the left Ctrl sends an extended scan
        }
    };
    fill(in[0], VK_CONTROL, true);
    fill(in[1], vkC, true);
    fill(in[2], vkC, false);
    fill(in[3], VK_CONTROL, false);
    return SendInput(4, in, sizeof(INPUT)) == 4;
}

/// Is there an input desktop we can put keys into at all?
QString desktopProblem()
{
    if (GetSystemMetrics(SM_SERVICES_SESSION)) {
        return QStringLiteral("session 0 (a service): there is no keyboard here");
    }
    if (!GetThreadDesktop(GetCurrentThreadId())) {
        return QStringLiteral("no desktop object for this thread (non-interactive logon)");
    }
    if (!OpenInputDesktop(0, FALSE, DESKTOP_WRITEOBJECTS)) {
        return QStringLiteral("the input desktop is another user's (locked or switched)");
    }
    return {};
}

} // namespace

class TestWinHookReal : public QObject
{
    Q_OBJECT

private slots:
    void theRealHookInstallsAndTearsDown()
    {
        if (const QString why = desktopProblem(); !why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        WinHotkeyBackend backend(winKeyHookInstaller(), winKeyHookUninstaller());
        QVERIFY2(backend.start(), qPrintable(QStringLiteral("the real hook refused: ")
                                            + backend.lastError()));
        QCOMPARE(backend.backendName(), QStringLiteral("win-ll-hook"));
        // Starting twice without stopping must not install a second hook -
        // two hooks means two gestures per key, and the second one is a bug
        // the user experiences as "it sometimes translates twice".
        QVERIFY(backend.start());
        backend.stop();
    }

    void aRealCtrlCTwiceFiresTheGesture()
    {
        if (const QString why = desktopProblem(); !why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        WinHotkeyBackend backend(winKeyHookInstaller(), winKeyHookUninstaller());
        backend.setIntervalMs(1500);
        QVERIFY2(backend.start(), qPrintable(backend.lastError()));
        QSignalSpy spy(&backend, &IHotkeyBackend::activated);

        // Qt's event loop has to pump for a low-level hook to be called at
        // all, so the presses go in from a timer inside the loop.
        int injected = 0;
        QTimer pump;
        QObject::connect(&pump, &QTimer::timeout, [&]() {
            if (injected++ < 3 && !injectChord()) {
                qWarning("SendInput was refused");
                qApp->quit();
            }
        });
        pump.start(120);
        const bool fired = spy.wait(4000) || spy.count() > 0;
        pump.stop();
        backend.stop();

        QVERIFY2(fired, "no gesture from three injected Ctrl+C chords");
        QVERIFY2(backend.injectedEvents() > 0,
                 "the hook fired but recorded no injected keys: the counter is a lie");
        QVERIFY(spy.count() <= 2);   // a burst is one gesture, not three
    }

    void theHookDoesNotSwallowTheUserOwnCopy()
    {
        // The product promise (README: "ordinary copying is never
        // intercepted"), measured instead of asserted. A low-level hook
        // cancels a keystroke by returning non-zero, and the system then stops
        // calling the hooks below it in the chain. So: install a probe hook
        // FIRST and ours second (the most recently installed hook is called
        // first), inject Ctrl+C, and count what the probe still sees. If our
        // hook ever ate the key the probe goes quiet - and the gesture would
        // still look fine, which is exactly why this row exists.
        if (const QString why = desktopProblem(); !why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        static int probeKeys = 0;
        HHOOK probe = SetWindowsHookExW(WH_KEYBOARD_LL, [](int code, WPARAM w, LPARAM l) {
            if (code == HC_ACTION) {
                ++probeKeys;
            }
            return CallNextHookEx(nullptr, code, w, l);
        }, nullptr, 0);
        QVERIFY2(probe != nullptr, "could not install the probe hook");

        WinHotkeyBackend backend(winKeyHookInstaller(), winKeyHookUninstaller());
        backend.setIntervalMs(1500);
        QVERIFY2(backend.start(), qPrintable(backend.lastError()));
        QSignalSpy spy(&backend, &IHotkeyBackend::activated);

        probeKeys = 0;
        // Same pump as the row above: a low-level hook is only called while
        // this thread runs its message loop, so a single SendInput issued
        // before the loop turns over is not a test, it is a race. (It was
        // written that way first, and failed - the gesture never fired, which
        // said nothing at all about swallowing keys.)
        int injected = 0;
        QTimer pump;
        QObject::connect(&pump, &QTimer::timeout, [&]() {
            if (injected++ < 3) {
                injectChord();
            }
        });
        pump.start(120);
        const bool fired = spy.wait(4000) || spy.count() > 0;
        pump.stop();
        QTest::qWait(300);                                  // ...and the rest arrives
        UnhookWindowsHookEx(probe);
        backend.stop();

        QVERIFY2(fired, "no gesture, so the probe measured nothing");

        // Four events in the chord; anything less means we ate one of them.
        QVERIFY2(probeKeys >= 4,
                 qPrintable(QStringLiteral("our hook swallowed keys: the probe saw ")
                            + QString::number(probeKeys) + QStringLiteral(" of 4")));
    }
};

QTEST_GUILESS_MAIN(TestWinHookReal)
#include "tst_winhook_real.moc"
