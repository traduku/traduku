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

        auto c1 = createClipboard(QStringLiteral("wayland"), both, qcb, r.runner());
        QCOMPARE(c1->backendName(), QString("wl-clipboard"));

        auto c2 = createClipboard(QStringLiteral("wayland"), none, qcb, r.runner());
        QCOMPARE(c2->backendName(), QString("qt"));

        auto c3 = createClipboard(QStringLiteral("xcb"), both, qcb, r.runner());
        QCOMPARE(c3->backendName(), QString("qt"));
    }

    void waylandWithoutToolsAdvisesInstallingWlClipboard()
    {
        // Measured on GNOME 50 (GNOME_PLAN.md §0b): a background wayland
        // client gets no data offer at all, so on mutter the Qt fallback is
        // blind - and a silent "" makes a missing wl-clipboard look like an
        // empty clipboard. The chosen backend must SAY what to install.
        auto *qcb = QGuiApplication::clipboard();

        auto blind = createClipboard(QStringLiteral("wayland"), finderWith({}), qcb);
        QCOMPARE(blind->backendName(), QString("qt"));
        QVERIFY2(!blind->diagnostic().isEmpty(),
                 "a backend that cannot see the clipboard stayed silent");
        QVERIFY2(blind->diagnostic().contains(QStringLiteral("wl-clipboard")),
                 qPrintable(blind->diagnostic()));

        // the real tools present: nothing to complain about
        auto good = createClipboard(QStringLiteral("wayland"),
                                    finderWith({"wl-paste", "wl-copy"}), qcb);
        QVERIFY(good->diagnostic().isEmpty());

        // X11 sessions: Qt reads the selection from the background fine,
        // and there the tools are not even used
        auto x11 = createClipboard(QStringLiteral("xcb"), finderWith({}), qcb);
        QVERIFY2(x11->diagnostic().isEmpty(), qPrintable(x11->diagnostic()));
    }
};

QTEST_MAIN(TestClipboards)
#include "tst_clipboards.moc"
