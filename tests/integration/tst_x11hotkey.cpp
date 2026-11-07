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

    void xwaylandMustUseRecordNotXi2Raw()
    {
        // Mutter injects Wayland keystrokes straight into the VDevice
        // (master keyboard); XI2 RawEvents fire for *slave* devices only,
        // so the raw listener is blind on XWayland under GNOME. X RECORD
        // sees every core event, including master injection. KWin (XTEST
        // forwarding) keeps raw events flowing and is unaffected.
        QCOMPARE(X11HotkeyBackend::chooseMode(true, true),
                 X11HotkeyBackend::Mode::Record);
        QCOMPARE(X11HotkeyBackend::chooseMode(true, false),
                 X11HotkeyBackend::Mode::Xi2Raw);
        QCOMPARE(X11HotkeyBackend::chooseMode(false, false),
                 X11HotkeyBackend::Mode::Record);
    }

    void xwaylandDetectedByExtensionNotByMissingGlx()
    {
        // Ubuntu 25.10's Xwayland advertises GLX (and RECORD), so the old
        // "no GLX => Xwayland" probe silently chose the blind raw listener.
        QVERIFY(X11HotkeyBackend::detectXwayland(true, false));
        QVERIFY(X11HotkeyBackend::detectXwayland(false, true));
        QVERIFY(!X11HotkeyBackend::detectXwayland(false, false));
    }

    void startWithoutDisplayIsGraceful()
    {
        if (qEnvironmentVariableIsEmpty("DISPLAY")) {
            QVERIFY(!m_backend.start());
            QVERIFY(!m_backend.start());   // repeatable
        } else {
            QVERIFY(m_backend.start());
            // The RECORD loop must be wakeable: a blocking
            // XRecordEnableContext would never return on an idle desktop.
            QElapsedTimer t;
            t.start();
            m_backend.stop();
            QVERIFY2(t.elapsed() < 2000,
                     qPrintable(QStringLiteral("stop() took %1 ms")
                                    .arg(t.elapsed())));
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

    void parseRecordLineAcceptsKeyEvents()
    {
        int type = 0;
        quint32 sym = 0, code = 0;
        QVERIFY(X11HotkeyBackend::parseRecordLine(
            QByteArray("K 65507 37"), &type, &sym, &code));
        QCOMPARE(type, KeyPress);
        QCOMPARE(sym, quint32(XK_Control_L));
        QCOMPARE(code, quint32(37));

        QVERIFY(X11HotkeyBackend::parseRecordLine(
            QByteArray("R 99 54"), &type, &sym, &code));
        QCOMPARE(type, KeyRelease);
        QCOMPARE(sym, quint32(99));
        QCOMPARE(code, quint32(54));
    }

    void parseRecordLineRejectsGarbage()
    {
        int type = 0;
        quint32 sym = 0, code = 0;
        QVERIFY(!X11HotkeyBackend::parseRecordLine(
            QByteArray("X 65507 37"), &type, &sym, &code));
        QVERIFY(!X11HotkeyBackend::parseRecordLine(
            QByteArray("K abc 37"), &type, &sym, &code));
        QVERIFY(!X11HotkeyBackend::parseRecordLine(
            QByteArray("K 65507"), &type, &sym, &code));
        QVERIFY(!X11HotkeyBackend::parseRecordLine(
            QByteArray(""), &type, &sym, &code));
        QVERIFY(!X11HotkeyBackend::parseRecordLine(
            QByteArray("K 65507 37 extra"), &type, &sym, &code));
    }
};

QTEST_GUILESS_MAIN(TestX11Hotkey)
#include "tst_x11hotkey.moc"
