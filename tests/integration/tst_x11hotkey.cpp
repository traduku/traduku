#include <QtTest>
#include <QSignalSpy>

#include "platform/x11/X11HotkeyBackend.h"

#include <X11/keysym.h>

using namespace lt;

// Exercises the X11 backend's event->detector pipeline with the exact
// values the XI2/RECORD loop forwards. start() is checked for graceful
// degradation: this sandbox has no X server, so it must return false
// (and the app falls back to the global shortcut), never crash.
class TestX11Hotkey : public QObject
{
    Q_OBJECT
    X11HotkeyBackend m_backend;
    int m_fires = 0;

    void chord()
    {
        m_backend.feedKeyEvent(KeyPress, XK_Control_L, 0);
        m_backend.feedKeyEvent(KeyPress, XK_c, 54);
        m_backend.feedKeyEvent(KeyRelease, XK_c, 54);
        m_backend.feedKeyEvent(KeyRelease, XK_Control_L, 0);
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        connect(&m_backend, &X11HotkeyBackend::activated,
                this, [this]() { ++m_fires; });
        m_backend.setIntervalMs(500);
    }

    void startWithoutDisplayIsGraceful()
    {
        if (qEnvironmentVariableIsEmpty("DISPLAY")) {
            QVERIFY(!m_backend.start());
            QVERIFY(!m_backend.start());   // repeatable
        } else {
            QVERIFY(m_backend.start());
            m_backend.stop();
        }
    }

    void singleCtrlCNoFire()
    {
        m_fires = 0;
        chord();
        QCOMPARE(m_fires, 0);
    }

    void doubleCtrlCFiresOnce()
    {
        m_fires = 0;
        chord();
        chord();
        QCOMPARE(m_fires, 1);
    }

    void cyrillicKeysymByNativeCode()
    {
        m_fires = 0;
        auto ruChord = [&]() {
            m_backend.feedKeyEvent(KeyPress, XK_Control_L, 0);
            m_backend.feedKeyEvent(KeyPress, XK_Cyrillic_es, 54);
            m_backend.feedKeyEvent(KeyRelease, XK_Cyrillic_es, 54);
            m_backend.feedKeyEvent(KeyRelease, XK_Control_L, 0);
            QCoreApplication::processEvents();
        };
        ruChord();
        ruChord();
        QCOMPARE(m_fires, 1);
    }

    void typingCNeverFires()
    {
        m_fires = 0;
        for (int i = 0; i < 30; ++i) {
            m_backend.feedKeyEvent(KeyPress, XK_c, 54);
            m_backend.feedKeyEvent(KeyRelease, XK_c, 54);
        }
        QCOMPARE(m_fires, 0);
    }
};

QTEST_GUILESS_MAIN(TestX11Hotkey)
#include "tst_x11hotkey.moc"
