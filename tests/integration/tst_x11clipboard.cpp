// X11Clipboard against a real X server.
//
// This is not a mock test: it connects to an actual server and speaks the
// ICCCM selection protocol - XSetSelectionOwner, XConvertSelection,
// SelectionNotify, SelectionRequest. A clipboard that only works against a
// fake is not a clipboard.
//
// Two rules keep it from damaging the machine it runs on:
//   - everything happens on a private selection atom (TRADUKU_TEST_SELECTION),
//     never on CLIPBOARD or PRIMARY, so the user's clipboard survives;
//   - the foreign clipboard owners the reader has to cope with are real X
//     clients started by this test, in their own thread with their own
//     connection - serving them from the test thread would deadlock the
//     round trip the test is trying to measure.
//
// Without a server the suite SKIPS and says so; the container gate sets
// TRADUKU_REQUIRE_X11=1 (with Xvfb available), and there a skip is a failure.
#include <QtTest>

#include "platform/x11/X11Clipboard.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <vector>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

using namespace lt;

namespace {

const char *const kTestSelection = "TRADUKU_TEST_SELECTION";
/// "Привет" - iconv-measured, because 0xD0 is "Р" in windows-1251 while it is
/// also the first byte of UTF-8 "П": the collision the reader must resolve.
const QByteArray CP1251_PRIVET = QByteArray::fromHex("CFF0E8E2E5F2");
const QString WELCOME = QString::fromUtf8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");

/// A second X client that owns the test selection and serves exactly the
/// targets the test wants it to - the way a legacy application would, with
/// no idea that anybody is watching.
class ForeignOwner
{
public:
    ForeignOwner(QStringList targets, QByteArray payload, QString wireType)
        : m_targets(std::move(targets)), m_payload(std::move(payload)),
          m_wireType(std::move(wireType))
    {
    }

    ~ForeignOwner()
    {
        stop();
    }

    bool start()
    {
        // Every X event mask, from the start: a client that takes ownership
        // before its client selects for SelectionNotify is a client whose
        // SelectionNotify is dropped, which is how the legacy-TEXT case
        // failed here before it was measured.
        m_display = XOpenDisplay(nullptr);
        if (!m_display) {
            return false;
        }
        m_selection = XInternAtom(m_display, kTestSelection, False);
        m_window = XCreateSimpleWindow(m_display,
                                       RootWindow(m_display, DefaultScreen(m_display)),
                                       0, 0, 1, 1, 0, 0, 0);
        XSetSelectionOwner(m_display, m_selection, m_window, CurrentTime);
        XSync(m_display, False);
        if (XGetSelectionOwner(m_display, m_selection) != m_window) {
            qWarning() << "ForeignOwner: the server did not give us the selection";
            XCloseDisplay(m_display);
            m_display = nullptr;
            return false;
        }
        m_running = true;
        m_thread = std::thread([this] { loop(); });
        return true;
    }

    void stop()
    {
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_display) {
            XCloseDisplay(m_display);
            m_display = nullptr;
        }
    }

private:
    void loop()
    {
        while (m_running) {
            while (XPending(m_display)) {
                XEvent ev;
                XNextEvent(m_display, &ev);
                if (ev.type == SelectionRequest) {
                    serve(&ev.xselectionrequest);
                }
            }
            usleep(5 * 1000);
        }
    }

    void serve(const XSelectionRequestEvent *rq)
    {
        char *raw = XGetAtomName(m_display, rq->target);
        const QString target = raw ? QString::fromUtf8(raw) : QString();
        if (raw) {
            XFree(raw);
        }

        XEvent reply;
        memset(&reply, 0, sizeof(reply));
        reply.xselection.type = SelectionNotify;
        reply.xselection.display = m_display;
        reply.xselection.requestor = rq->requestor;
        reply.xselection.selection = rq->selection;
        reply.xselection.time = rq->time;
        reply.xselection.target = rq->target;
        reply.xselection.property = None;

        if (target == QLatin1String("TARGETS")) {
            std::vector<Atom> offered;
            for (const QString &t : m_targets) {
                offered.push_back(XInternAtom(m_display, t.toUtf8().constData(), False));
            }
            XChangeProperty(m_display, rq->requestor, rq->property, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char *>(offered.data()),
                            static_cast<int>(offered.size()));
            reply.xselection.property = rq->property;
        } else if (m_targets.contains(target)) {
            // The property type is the requester's own target - that is what
            // tells the reader which charset it owes the bytes.
            XChangeProperty(m_display, rq->requestor, rq->property,
                            XInternAtom(m_display, m_wireType.toUtf8().constData(), False),
                            8, PropModeReplace,
                            reinterpret_cast<const unsigned char *>(m_payload.constData()),
                            m_payload.size());
            reply.xselection.property = rq->property;
        }
        if (reply.xselection.property != None) {
            XSendEvent(m_display, rq->requestor, False, 0, &reply);
            XFlush(m_display);
        }
    }

    QStringList m_targets;
    QByteArray m_payload;
    QString m_wireType;
    Display *m_display = nullptr;
    Window m_window = 0;
    Atom m_selection = None;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

/// Read one target off the test selection as an unrelated client would:
/// own connection, own window, XConvertSelection and a wait. Used from the
/// test thread or from another one, depending on which side of the round
/// trip needs to be pumping events.
QString strangerFetch(const QString &target, int timeoutMs = 3000)
{
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        return {};
    }
    const Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                                           0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, win, SelectionNotify);
    const Atom sel = XInternAtom(dpy, kTestSelection, False);
    const Atom targetAtom = XInternAtom(dpy, target.toUtf8().constData(), False);
    const Atom prop = XInternAtom(dpy, "TRADUKU_STRANGER", False);
    XDeleteProperty(dpy, win, prop);
    XConvertSelection(dpy, sel, targetAtom, prop, win, CurrentTime);
    XFlush(dpy);

    QString out;
    QElapsedTimer timer;
    timer.start();
    while (out.isEmpty() && timer.elapsed() < timeoutMs) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == SelectionNotify && ev.xselection.property != None) {
                Atom type = None;
                int fmt = 0;
                unsigned long n = 0, rest = 0;
                unsigned char *data = nullptr;
                XGetWindowProperty(dpy, win, prop, 0, 65536, False, AnyPropertyType,
                                   &type, &fmt, &n, &rest, &data);
                out = QString::fromUtf8(reinterpret_cast<const char *>(data),
                                        static_cast<int>(n * (fmt / 8)));
                XFree(data);
            }
        }
        usleep(2000);
    }
    XCloseDisplay(dpy);
    return out;
}

/// Reachable server at all?
bool serverAvailable()
{
    X11Clipboard::Options opt;
    return X11Clipboard::isUsable(opt);
}

} // namespace

class TestX11Clipboard : public QObject
{
    Q_OBJECT
private:
    void requireServer()
    {
        if (serverAvailable()) {
            return;
        }
        if (qEnvironmentVariableIsSet("TRADUKU_REQUIRE_X11")) {
            QFAIL("TRADUKU_REQUIRE_X11 is set, so a clipboard that cannot reach "
                  "an X server is a failure, not a skip");
        }
        QSKIP("no X server reachable (headless build host): the X11 clipboard is "
              "tested for real in the deb-builder gate under Xvfb, and on the "
              "grid guests");
    }

    X11Clipboard::Options testSelection()
    {
        X11Clipboard::Options opt;
        opt.selection = QString::fromLatin1(kTestSelection);
        return opt;
    }

private slots:
    void writeThenAnotherClientReadsItBack()
    {
        requireServer();
        X11Clipboard writer(testSelection());
        const QString wanted = WELCOME + QStringLiteral(" — Traduku ✓");
        QVERIFY2(writer.setText(wanted), qPrintable(writer.diagnostic()));

        // Taking ownership is a promise: every later SelectionRequest has to
        // be answered, or the requester hangs. Nothing pumps the writer here
        // except this thread - which is the point of the test. In the app the
        // pumping is the Qt event loop's job (QSocketNotifier on the
        // connection fd); a QSystemTrayIcon-style "write and forget" owner
        // would leave every paste waiting on a 500 ms timeout.
        std::atomic<bool> stop{false};
        std::atomic<int> served{0};
        std::thread pump([&] {
            while (!stop) {
                if (writer.processEvents(20)) {
                    ++served;
                }
            }
        });
        const QString fetched = strangerFetch(QStringLiteral("UTF8_STRING"));
        stop = true;
        pump.join();

        QVERIFY2(served > 0,
                 "the owner never served a single request: ownership without "
                 "an event pump is a clipboard that hangs other people's paste");
        QCOMPARE(fetched, wanted);
    }

    void theQtEventLoopKeepsUsServing()
    {
        requireServer();
        // The same promise as above, but nothing pumps the owner by hand:
        // in the app the only thing running is the Qt event loop. If the
        // class does not watch its own connection fd, an autostarted traduku
        // that put a translation on the clipboard would hang every paste
        // into every other application - the failure is invisible from our
        // side and looks like a broken editor.
        X11Clipboard writer(testSelection());
        QVERIFY2(writer.setText(WELCOME), qPrintable(writer.diagnostic()));

        QString fetched;
        std::thread stranger([&] { fetched = strangerFetch(QStringLiteral("UTF8_STRING")); });
        for (int i = 0; i < 100 && fetched.isEmpty(); ++i) {
            QTest::qWait(20);              // the event loop, and nothing else
        }
        stranger.join();
        QCOMPARE(fetched, WELCOME);
    }

    void weOwnTheSelectionAfterWriting()
    {
        requireServer();
        X11Clipboard cb(testSelection());
        QVERIFY(cb.setText(WELCOME));
        Display *dpy = XOpenDisplay(nullptr);
        QVERIFY2(dpy, "cannot open a second connection to check ownership");
        const Window owner = XGetSelectionOwner(dpy, XInternAtom(dpy, kTestSelection, False));
        XCloseDisplay(dpy);
        QVERIFY2(owner != 0, "the X server says nobody owns the selection after we wrote it");
    }

    void readsUtf8FromAForeignOwner()
    {
        requireServer();
        ForeignOwner owner({QStringLiteral("UTF8_STRING"), QStringLiteral("TEXT")},
                           WELCOME.toUtf8(), QStringLiteral("UTF8_STRING"));
        QVERIFY(owner.start());
        X11Clipboard cb(testSelection());
        QCOMPARE(cb.text(), WELCOME);
    }

    void readsLegacyTextOfferDecodedByItsRealCharset()
    {
        requireServer();
        // The measured garble: the only offer is TEXT, the bytes are
        // windows-1251, and there is no charset anywhere to be read.
        ForeignOwner owner({QStringLiteral("TEXT")}, CP1251_PRIVET, QStringLiteral("TEXT"));
        QVERIFY(owner.start());
        X11Clipboard cb(testSelection());
        QCOMPARE(cb.text(), WELCOME);
    }

    void nonTextOffersReadAsEmpty()
    {
        requireServer();
        ForeignOwner owner({QStringLiteral("image/png"), QStringLiteral("text/uri-list")},
                           QByteArray("not text"), QStringLiteral("image/png"));
        QVERIFY(owner.start());
        X11Clipboard cb(testSelection());
        QVERIFY2(cb.text().isEmpty(),
                 qPrintable(QStringLiteral("an image selection was read as: ") + cb.text()));
    }

    void anOwnerThatDoesNotAnswerDoesNotHangUs()
    {
        requireServer();
        // Own the selection, then never serve: the reader must give up and
        // say empty, not sit in XNextEvent forever. (The deadline is the
        // reason this test also takes the time it does.)
        Display *dpy = XOpenDisplay(nullptr);
        QVERIFY(dpy);
        const Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                                               0, 0, 1, 1, 0, 0, 0);
        const Atom sel = XInternAtom(dpy, kTestSelection, False);
        XSetSelectionOwner(dpy, sel, win, CurrentTime);
        XSync(dpy, False);

        QElapsedTimer timer;
        timer.start();
        X11Clipboard cb(testSelection());
        QVERIFY(cb.text().isEmpty());
        QVERIFY2(timer.elapsed() < 5000,
                 qPrintable(QStringLiteral("the silent owner cost %1 ms")
                            .arg(timer.elapsed())));
        XCloseDisplay(dpy);
    }

    void diagnosticNamesTheServerWhenWeCannotConnect()
    {
        X11Clipboard::Options opt;
        opt.display = QStringLiteral(":999");
        opt.selection = QString::fromLatin1(kTestSelection);
        X11Clipboard cb(opt);
        QVERIFY(cb.text().isEmpty());
        QVERIFY2(!cb.diagnostic().isEmpty(),
                 "a clipboard that cannot connect has to say why, not return empty "
                 "and let the user blame their own copy");
    }
};

QTEST_GUILESS_MAIN(TestX11Clipboard)
#include "tst_x11clipboard.moc"
