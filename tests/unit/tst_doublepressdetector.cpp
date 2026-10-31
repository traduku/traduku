#include <QtTest>
#include "core/doublepressdetector.h"

using namespace lt;

// Fake clock so tests control time precisely.
class TestDoublePress : public QObject
{
    Q_OBJECT
    DoublePressDetector m_det;
    qint64 m_now = 1000;
    int m_fires = 0;

    void chord(qint64 advanceMs = 50)
    {
        m_now += advanceMs;
        m_det.keyEvent(DoublePressDetector::Press, KeyCtrlL, 0);
        m_det.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyCtrlL, 0);
    }

private slots:
    void initTestCase()
    {
        connect(&m_det, &DoublePressDetector::activated,
                this, [this]() { ++m_fires; });
    }

    void init()
    {
        m_now = 1000;
        m_fires = 0;
        m_det.reset();
        m_det.setIntervalMs(500);
        m_det.setClock([this]() { return m_now; });
        m_det.setModifierProbe({});
    }

    void singleChordDoesNotFire()
    {
        chord();
        QCOMPARE(m_fires, 0);
    }

    void doubleChordFiresOnce()
    {
        chord();
        m_now += 200;
        chord();
        QCOMPARE(m_fires, 1);
    }

    void thirdChordPairsWithSecond()
    {
        chord();
        m_now += 400;
        chord();                 // fires (pair 1-2)
        QCOMPARE(m_fires, 1);
        m_now += 100;
        chord();                 // fresh first press of a new gesture
        QCOMPARE(m_fires, 1);
        m_now += 100;
        chord();                 // pair 3-4
        QCOMPARE(m_fires, 2);
    }

    void outsideWindowDoesNotFire()
    {
        chord();
        m_now += 900;            // > 500 ms interval
        chord();
        QCOMPARE(m_fires, 0);
    }

    void autoRepeatCountsAsOnePress()
    {
        // ctrl held, 'c' hammered by key repeat, released; gesture repeated
        m_now += 50;
        m_det.keyEvent(DoublePressDetector::Press, KeyCtrlL, 0);
        for (int i = 0; i < 5; ++i) {
            m_det.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
        }
        m_det.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyCtrlL, 0);
        QCOMPARE(m_fires, 0);
        m_now += 100;
        m_det.keyEvent(DoublePressDetector::Press, KeyCtrlL, 0);
        for (int i = 0; i < 5; ++i) {
            m_det.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
        }
        m_det.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyCtrlL, 0);
        QCOMPARE(m_fires, 1);    // exactly one
    }

    void typingCWithoutCtrlNeverFires()
    {
        for (int i = 0; i < 20; ++i) {
            m_now += 30;
            m_det.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
            m_det.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        }
        QCOMPARE(m_fires, 0);
    }

    void ctrlAloneNeverFires()
    {
        for (int i = 0; i < 4; ++i) {
            m_now += 100;
            m_det.keyEvent(DoublePressDetector::Press, KeyCtrlL, 0);
            m_det.keyEvent(DoublePressDetector::Release, KeyCtrlL, 0);
        }
        QCOMPARE(m_fires, 0);
    }

    void nonLatinLayoutViaNativeCode()
    {
        // Russian layout: keysym for the C key is Cyrillic 'с' (0x0441),
        // only the native keycode (54) identifies it layout-independently.
        auto ruChord = [&]() {
            m_now += 60;
            m_det.keyEvent(DoublePressDetector::Press, KeyCtrlL, 0);
            m_det.keyEvent(DoublePressDetector::Press, 0x0441, KeyNativeC);
            m_det.keyEvent(DoublePressDetector::Release, 0x0441, KeyNativeC);
            m_det.keyEvent(DoublePressDetector::Release, KeyCtrlL, 0);
        };
        ruChord();
        m_now += 150;
        ruChord();
        QCOMPARE(m_fires, 1);
    }

    void modifierProbeFallback()
    {
        // Some Wayland sessions sync modifier *state* without forwarding
        // Ctrl key events: the XKB probe must be consulted.
        m_det.setModifierProbe([this]() { return true; });
        auto probeChord = [&]() {
            m_now += 60;
            m_det.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
            m_det.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        };
        probeChord();
        m_now += 150;
        probeChord();
        QCOMPARE(m_fires, 1);
        // ...and with the probe reporting "no modifiers", typing is safe:
        m_det.setModifierProbe([this]() { return false; });
        m_det.reset();
        m_now += 1000;           // leave the previous gesture window
        probeChord();
        m_now += 100;
        probeChord();
        QCOMPARE(m_fires, 1);
    }

    void rightCtrlAlsoWorks()
    {
        m_now += 50;
        m_det.keyEvent(DoublePressDetector::Press, KeyCtrlR, 0);
        m_det.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyCtrlR, 0);
        m_now += 100;
        m_det.keyEvent(DoublePressDetector::Press, KeyCtrlR, 0);
        m_det.keyEvent(DoublePressDetector::Press, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyC, KeyNativeC);
        m_det.keyEvent(DoublePressDetector::Release, KeyCtrlR, 0);
        QCOMPARE(m_fires, 1);
    }

    void resetClearsGestureState()
    {
        chord();
        m_det.reset();
        m_now += 100;
        chord();                 // second press of previous gesture forgotten
        QCOMPARE(m_fires, 0);
    }

    void intervalChangeApplies()
    {
        m_det.setIntervalMs(50);
        chord();
        m_now += 80;             // now outside the new 50ms window
        chord();
        QCOMPARE(m_fires, 0);
    }
};

QTEST_GUILESS_MAIN(TestDoublePress)
#include "tst_doublepressdetector.moc"
