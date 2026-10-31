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
struct FakeRunner
{
    QList<QPair<QString, QStringList>> calls;
    QByteArray stdoutData;
    int rc = 0;
    WlClipboard::ToolRunner runner()
    {
        return [this](const QString &prog, const QStringList &args,
                      const QByteArray &in, QByteArray *out) {
            calls.append({prog, args});
            if (!in.isNull() && !in.isEmpty() && out) {
                *out = QByteArray("echo:") + in;   // unused path
            }
            if (out) {
                *out = stdoutData;
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
    void wlReadParsesToolOutput()
    {
        FakeRunner r;
        r.stdoutData = QByteArray("clipboard content");
        WlClipboard cb(r.runner(), finderWith({"wl-paste", "wl-copy"}));
        QCOMPARE(cb.text(), QString("clipboard content"));
        QCOMPARE(r.calls.size(), 1);
        QCOMPARE(r.calls.at(0).first, QString("wl-paste"));
        QVERIFY(r.calls.at(0).second.contains(QStringLiteral("-n")));
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

        // Wayland with wl-clipboard installed -> tool backend
        auto c1 = createClipboard(QStringLiteral("wayland"), both, qcb, r.runner());
        QCOMPARE(c1->backendName(), QString("wl-clipboard"));

        // Wayland without the tools -> Qt fallback (will warn at runtime)
        auto c2 = createClipboard(QStringLiteral("wayland"), none, qcb, r.runner());
        QCOMPARE(c2->backendName(), QString("qt"));

        // X11 / anything else -> Qt
        auto c3 = createClipboard(QStringLiteral("xcb"), both, qcb, r.runner());
        QCOMPARE(c3->backendName(), QString("qt"));
    }
};

QTEST_MAIN(TestClipboards)
#include "tst_clipboards.moc"
