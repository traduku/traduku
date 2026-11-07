#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>

#include "interfaces/iclipboard.h"
#include "platform/qt/QtClipboard.h"
#include "platform/wl/WlClipboard.h"
#include "platform/wl/ClipboardFactory.h"

using namespace lt;

namespace {
// Records invocations, returns canned output. No real processes spawn.
// Understands the wl-paste protocol: `-l` lists offered types, `-t X`
// returns the payload offered under type X.
struct FakeRunner
{
    QList<QPair<QString, QStringList>> calls;
    QByteArray stdoutData;                       // payload for -t
    QList<QByteArray> responses;                 // -l first, -t second
    int responseIndex = 0;
    int rc = 0;

    WlClipboard::ToolRunner runner()
    {
        return [this](const QString &prog, const QStringList &args,
                      const QByteArray &in, QByteArray *out) {
            calls.append({prog, args});
            Q_UNUSED(in);
            if (out) {
                *out = responses.isEmpty() ? stdoutData
                                           : responses.at(qMin(responseIndex,
                                                                responses.size() - 1));
                ++responseIndex;
            }
            return rc;
        };
    }
};
auto finderWith(const QStringList &present)
{
    return [present](const QString &tool) {
        return present.contains(tool) ? QStringLiteral("/usr/bin/") + tool
                                      : QString();
    };
}
} // namespace

class TestClipboards : public QObject
{
    Q_OBJECT
private slots:
    void wlReadNegotiatesBestType()
    {
        FakeRunner r;
        r.responses << QByteArray("text/html\ntext/plain;charset=utf-8\nUTF8_STRING\n")
                    << QByteArray("Привет мир");
        WlClipboard cb(r.runner(), finderWith({"wl-paste", "wl-copy"}));
        QCOMPARE(cb.text(), QString("Привет мир"));
        // First call lists offers, second fetches the BEST one (utf-8),
        // never bare "text/plain" when a charset-tagged offer exists.
        QCOMPARE(r.calls.size(), 2);
        QVERIFY(r.calls.at(0).second.contains(QStringLiteral("-l")));
        QCOMPARE(r.calls.at(1).second,
                 (QStringList{"-n", "-t", QStringLiteral("text/plain;charset=utf-8")}));
    }

    void wlReadPrefersCharsetVariant()
    {
        // The lib.ru scenario: browser offers a windows-1251 text target;
        // the bytes must be decoded per the charset in the type name.
        FakeRunner r;
        r.responses << QByteArray("text/html\ntext/plain;charset=windows-1251\n")
                    << QByteArray("\xCF\xF0\xE8\xE2\xE5\xF2");   // "Привет" cp1251
        WlClipboard cb(r.runner(), finderWith({"wl-paste", "wl-copy"}));
        QCOMPARE(cb.text(), QString("Привет"));
    }

    void pickBestTypeRules()
    {
        QCOMPARE(WlClipboard::pickBestType({"text/html", "text/plain;charset=utf-8",
                                            "UTF8_STRING"}),
                 QString("text/plain;charset=utf-8"));
        QCOMPARE(WlClipboard::pickBestType({"text/plain", "text/html"}),
                 QString("text/plain"));
        QCOMPARE(WlClipboard::pickBestType({"text/html", "UTF8_STRING"}),
                 QString("UTF8_STRING"));
        QCOMPARE(WlClipboard::pickBestType({"text/html", "text/plain;charset=windows-1251"}),
                 QString("text/plain;charset=windows-1251"));
        // utf-8 wins over a legacy charset
        QCOMPARE(WlClipboard::pickBestType({"text/plain;charset=windows-1251",
                                            "text/plain;charset=utf-8"}),
                 QString("text/plain;charset=utf-8"));
        QVERIFY(WlClipboard::pickBestType({"text/html", "image/png"}).isEmpty());
    }

    void decodePerCharset()
    {
        QCOMPARE(WlClipboard::decodeWithCharset(
                     QByteArray("plain ascii"), QStringLiteral("text/plain")),
                 QString("plain ascii"));
        QCOMPARE(WlClipboard::decodeWithCharset(
                     QString::fromUtf8("Привет").toUtf8(),
                     QStringLiteral("text/plain;charset=utf-8")),
                 QString("Привет"));
        QCOMPARE(WlClipboard::decodeWithCharset(
                     QByteArray("\xCF\xF0\xE8\xE2\xE5\xF2"),
                     QStringLiteral("text/plain;charset=windows-1251")),
                 QString("Привет"));
        QCOMPARE(WlClipboard::decodeWithCharset(
                     QByteArray("caf\xe9"),
                     QStringLiteral("text/plain;charset=iso-8859-1")),
                 QString("café"));
        // unknown charset -> UTF-8 with replacement chars, never a crash
        QVERIFY(!WlClipboard::decodeWithCharset(QByteArray("x"),
                                                QStringLiteral("text/plain;charset=bogus-9000"))
                     .isEmpty());
    }

    void wlReadBareTextPlainWhenNoCharsetOffered()
    {
        FakeRunner r;
        r.responses << QByteArray("text/plain\ntext/html\n")
                    << QByteArray("clipboard content");
        WlClipboard cb(r.runner(), finderWith({"wl-paste", "wl-copy"}));
        QCOMPARE(cb.text(), QString("clipboard content"));
        QCOMPARE(r.calls.at(1).second,
                 (QStringList{"-n", "-t", QStringLiteral("text/plain")}));
    }

    void wlWriteSendsTextOnStdin()
    {
        FakeRunner r;
        WlClipboard cb(r.runner(), finderWith({"wl-paste", "wl-copy"}));
        QVERIFY(cb.setText(QStringLiteral("перевод")));
        QCOMPARE(r.calls.size(), 1);
        QCOMPARE(r.calls.at(0).first, QString("wl-copy"));
    }

    void wlFailureSurfaces()
    {
        FakeRunner r;
        r.rc = 1;
        WlClipboard cb(r.runner(), finderWith({"wl-paste", "wl-copy"}));
        QVERIFY(!cb.setText(QStringLiteral("x")));
        QVERIFY(cb.text().isEmpty());
    }

    void wlAvailabilityByTools()
    {
        FakeRunner r;
        QVERIFY(WlClipboard::isUsable(r.runner(), finderWith({"wl-paste", "wl-copy"})));
        QVERIFY(!WlClipboard::isUsable(r.runner(), finderWith({"wl-paste"})));
        QVERIFY(!WlClipboard::isUsable(r.runner(), finderWith({})));
    }

    void qtClipboardRoundTrip()
    {
        QtClipboard cb(QGuiApplication::clipboard());
        cb.setText(QStringLiteral("hello qt"));
        QCOMPARE(cb.text(), QString("hello qt"));
        QCOMPARE(cb.backendName(), QString("qt"));
    }

    void factoryPicksBestBackend()
    {
        FakeRunner r;
        auto both = finderWith({"wl-paste", "wl-copy"});
        auto none = finderWith({});
        auto *qcb = QGuiApplication::clipboard();

        // xAvailable is passed explicitly throughout: the host running this
        // test may or may not have an X server, and the machine must not be
        // able to decide what the test believes.
        const auto noX = [] { return false; };
        const auto haveX = [] { return true; };

        auto c1 = createClipboard(QStringLiteral("wayland"), both, qcb, r.runner(), noX);
        QCOMPARE(c1->backendName(), QString("wl-clipboard"));

        auto c2 = createClipboard(QStringLiteral("wayland"), none, qcb, r.runner(), noX);
        QCOMPARE(c2->backendName(), QString("qt"));

        auto c3 = createClipboard(QStringLiteral("xcb"), both, qcb, r.runner(), noX);
        QCOMPARE(c3->backendName(), QString("qt"));

        // The case that made the X11 reader necessary: wayland, no
        // wl-clipboard (Arch, where it is AUR-only), XWayland reachable.
        // Qt here reads a plain TEXT offer as latin-1 and returns mojibake,
        // so choosing it is not a neutral fallback.
        auto c4 = createClipboard(QStringLiteral("wayland"), none, qcb, r.runner(), haveX);
        QCOMPARE(c4->backendName(), QString("x11"));

        // X11 session with a live server: the X11 reader negotiates, Qt's xcb
        // backend does not.
        auto c5 = createClipboard(QStringLiteral("xcb"), none, qcb, r.runner(), haveX);
        QCOMPARE(c5->backendName(), QString("x11"));

        // wl-clipboard wins on wayland even with X available: the compositor
        // owns the clipboard, XWayland only mirrors it.
        auto c6 = createClipboard(QStringLiteral("wayland"), both, qcb, r.runner(), haveX);
        QCOMPARE(c6->backendName(), QString("wl-clipboard"));

        // The report decorates the session string it hands back
        // ("wayland (qpa=offscreen)") to say what it actually measured. That
        // must not be allowed to change the product: an exact comparison made
        // a decorated wayland box lose wl-clipboard and read through X11.
        auto c7 = createClipboard(QStringLiteral("wayland (qpa=offscreen)"), both, qcb,
                                  r.runner(), haveX);
        QCOMPARE(c7->backendName(), QString("wl-clipboard"));
        auto c8 = createClipboard(QStringLiteral("wayland (no display here)"), none, qcb,
                                  r.runner(), haveX);
        QCOMPARE(c8->backendName(), QString("x11"));
    }

    void forcedBackendIsHonouredOnlyWhenItCanWork()
    {
        // TRADUKU_CLIPBOARD is the knob a user reaches for when a paste goes
        // wrong. Honouring it into a backend whose prerequisite is missing
        // would give an empty clipboard and no reason - the exact failure the
        // knob exists to investigate.
        auto *qcb = QGuiApplication::clipboard();
        const auto noX = [] { return false; };
        const auto haveX = [] { return true; };

        QCOMPARE(chooseClipboard(true, true, true, QStringLiteral("qt")),
                 ClipboardBackend::Qt);
        QCOMPARE(chooseClipboard(true, true, true, QStringLiteral("WL")),
                 ClipboardBackend::WlClipboardTool);
        QCOMPARE(chooseClipboard(true, false, true, QStringLiteral("wl")),
                 ClipboardBackend::X11);          // forced, but nothing to force
        QCOMPARE(chooseClipboard(true, true, false, QStringLiteral("x11")),
                 ClipboardBackend::WlClipboardTool);
        QCOMPARE(chooseClipboard(false, false, false, QString()),
                 ClipboardBackend::Qt);
        // wl-clipboard on an X11 session is no reason to shell out: Qt reads
        // a real X selection, and without a server there is nothing to prefer.
        QCOMPARE(chooseClipboard(false, true, false, QString()), ClipboardBackend::Qt);

        // And through the factory: forcing qt must beat a reachable server.
        qputenv("TRADUKU_CLIPBOARD", "qt");
        auto forced = createClipboard(QStringLiteral("xcb"), finderWith({"wl-paste", "wl-copy"}),
                                     qcb, {}, haveX);
        QCOMPARE(forced->backendName(), QString("qt"));
        qunsetenv("TRADUKU_CLIPBOARD");
    }

    void aChosenBackendNeverBlamesSomethingThatIsFine()
    {
        // On ubuntu-gnome, TRADUKU_CLIPBOARD=qt printed "the wl-clipboard
        // tools are not installed (sudo apt install wl-clipboard)" on a box
        // where they are installed and working: the hint was attached to
        // "we are on wayland and ended up in Qt", not to "the tools are
        // missing". A diagnostic that sends the user to install what they
        // already have is worse than silence.
        auto *qcb = QGuiApplication::clipboard();
        const auto noX = [] { return false; };
        const auto haveX = [] { return true; };

        qputenv("TRADUKU_CLIPBOARD", "qt");
        auto forced = createClipboard(QStringLiteral("wayland"),
                                      finderWith({"wl-paste", "wl-copy"}), qcb, {}, noX);
        QCOMPARE(forced->backendName(), QString("qt"));
        QVERIFY2(!forced->diagnostic().contains(QStringLiteral("not installed")),
                 qPrintable(QStringLiteral("blamed a missing package that is there: ")
                            + forced->diagnostic()));
        // Same when the user opted out of a working X server.
        auto forcedX = createClipboard(QStringLiteral("xcb"), finderWith({}), qcb, {}, haveX);
        QCOMPARE(forcedX->backendName(), QString("qt"));
        QVERIFY(forcedX->diagnostic().isEmpty());
        qunsetenv("TRADUKU_CLIPBOARD");

        // And the honest case still speaks: wayland, no tools, nothing to
        // force - that is the one time "install wl-clipboard" is the truth.
        auto blind = createClipboard(QStringLiteral("wayland"), finderWith({}), qcb, {}, noX);
        QCOMPARE(blind->backendName(), QString("qt"));
        QVERIFY(blind->diagnostic().contains(QStringLiteral("wl-clipboard")));
    }

    void aForcedBackendThatWasIgnoredSaysWhy()
    {
        // The knob exists so a user can separate "the reader is broken" from
        // "the compositor is broken". Being ignored in silence destroys that:
        // TRADUKU_CLIPBOARD=x11 on a machine where X is unreachable, or =wl
        // where the tools are missing, has to come back with a reason, not
        // with a different backend and a smile. Measured on ubuntu-gnome,
        // where forcing x11 quietly returned wl-clipboard.
        QCOMPARE(forcedBackendProblem(QStringLiteral("qt"), QStringLiteral("x11"), false,
                                      QStringLiteral("Xlib: connection refused")),
                 QStringLiteral("TRADUKU_CLIPBOARD=x11 could not be used: "
                                "Xlib: connection refused"));
        QCOMPARE(forcedBackendProblem(QStringLiteral("qt"), QStringLiteral("wl"), false,
                                      QString()),
                 QStringLiteral("TRADUKU_CLIPBOARD=wl could not be used: "
                                "wl-paste/wl-copy are not installed or do not run"));
        // honoured: nothing to report
        QVERIFY(forcedBackendProblem(QStringLiteral("x11"), QStringLiteral("x11"), true,
                                     QString()).isEmpty());
        // no knob set: never a complaint
        QVERIFY(forcedBackendProblem(QStringLiteral("qt"), QString(), false,
                                     QStringLiteral("whatever")).isEmpty());
        // An unusable reason we do not know the shape of must still be shown.
        QVERIFY(forcedBackendProblem(QStringLiteral("qt"), QStringLiteral("x11"), false,
                                     QString()).contains(QStringLiteral("no reason given")));
    }

    void waylandWithoutToolsAdvisesInstallingWlClipboard()
    {
        // Measured on GNOME 50 (ROADMAP.md §2): a background wayland
        // client gets no data offer at all, so on mutter the Qt fallback is
        // blind - and a silent "" makes a missing wl-clipboard look like an
        // empty clipboard. The chosen backend must SAY what to install.
        auto *qcb = QGuiApplication::clipboard();

        auto blind = createClipboard(QStringLiteral("wayland"), finderWith({}), qcb, {},
                                    [] { return false; });
        QCOMPARE(blind->backendName(), QString("qt"));
        QVERIFY2(!blind->diagnostic().isEmpty(),
                 "a backend that cannot see the clipboard stayed silent");
        QVERIFY2(blind->diagnostic().contains(QStringLiteral("wl-clipboard")),
                 qPrintable(blind->diagnostic()));

        // the real tools present: nothing to complain about
        auto good = createClipboard(QStringLiteral("wayland"),
                                    finderWith({"wl-paste", "wl-copy"}), qcb, {},
                                    [] { return false; });
        QVERIFY(good->diagnostic().isEmpty());

        // X11 sessions: Qt reads the selection from the background fine,
        // and there the tools are not even used
        auto x11 = createClipboard(QStringLiteral("xcb"), finderWith({}), qcb, {},
                                  [] { return false; });
        QVERIFY2(x11->diagnostic().isEmpty(), qPrintable(x11->diagnostic()));
    }
};

QTEST_MAIN(TestClipboards)
#include "tst_clipboards.moc"
