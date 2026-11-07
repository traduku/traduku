#include <QtTest>

#include "app/BackendInfo.h"

using namespace lt;

// --backend-info is what an operator (and tests/e2e) runs to find out why
// nothing happens. The report is a pure text render of a struct, so the
// wording - the part that has to be actionable - is unit-testable.
class TestBackendInfo : public QObject
{
    Q_OBJECT
    BackendReport m_r;

private slots:
    void init()
    {
        m_r.shell = QStringLiteral("gnome");
        m_r.sessionType = QStringLiteral("wayland");
        m_r.desktop = QStringLiteral("ubuntu:GNOME");
        m_r.clipboard = QStringLiteral("wl-clipboard");
        m_r.hotkeyAttempts = {{QStringLiteral("evdev"), QStringLiteral("chosen")}};
        m_r.hotkeyChosen = QStringLiteral("evdev");
        m_r.gestureSource = QStringLiteral("evdev /dev/input/event*");
        m_r.notifier = QStringLiteral("fdo");
        m_r.tray = QStringLiteral("sni");
    }

    void healthyReportNamesEveryLayer()
    {
        const QString s = formatBackendInfo(m_r);
        QVERIFY2(s.contains(QStringLiteral("shell: gnome")), qPrintable(s));
        QVERIFY(s.contains(QStringLiteral("session: wayland")));
        QVERIFY(s.contains(QStringLiteral("desktop: ubuntu:GNOME")));
        QVERIFY(s.contains(QStringLiteral("clipboard: wl-clipboard")));
        QVERIFY(s.contains(QStringLiteral("gesture: evdev /dev/input/event*")));
        QVERIFY(s.contains(QStringLiteral("notifier: fdo")));
        QVERIFY(s.contains(QStringLiteral("tray: sni")));
        QVERIFY2(!s.contains(QStringLiteral("hint")),
                 "a healthy machine must not print hints:\n" + s.toUtf8());
    }

    void clipboardHintIsPassedThroughVerbatim()
    {
        // The missing-package instruction comes from the backend itself
        // (IClipboard::diagnostic); it must reach the operator untouched.
        m_r.clipboard = QStringLiteral("qt");
        m_r.clipboardHint = QStringLiteral("install wl-clipboard");
        const QString s = formatBackendInfo(m_r);
        QVERIFY2(s.contains(QStringLiteral("install wl-clipboard")), qPrintable(s));
        QVERIFY(s.contains(QStringLiteral("clipboard: qt")));
    }

    void inputAccessProblemPointsAtTheRule()
    {
        m_r.gestureSource.clear();
        m_r.inputAccess = QStringLiteral("no readable keyboard");
        const QString s = formatBackendInfo(m_r);
        QVERIFY2(s.contains(QStringLiteral("no readable keyboard")), qPrintable(s));
        QVERIFY2(s.contains(QStringLiteral("70-traduku-uaccess.rules")),
                 "the operator must be told how to fix it:\n" + s.toUtf8());
        QVERIFY2(s.contains(QStringLiteral("gesture: none")), qPrintable(s));
    }

    void showsWhatHappensToModelReasoning()
    {
        // "the translation is slow" is usually a reasoning model streaming a
        // hidden channel we discard - the report has to say whether we asked
        // the server to skip it.
        m_r.reasoning = QStringLiteral("skipped (request options sent)");
        QVERIFY2(formatBackendInfo(m_r).contains(QStringLiteral("reasoning: skipped")),
                 qPrintable(formatBackendInfo(m_r)));

        m_r.reasoning = QStringLiteral("NOT disabled - the model may stream hidden reasoning");
        QVERIFY(formatBackendInfo(m_r).contains(QStringLiteral("reasoning: NOT disabled")));

        m_r.reasoning.clear();          // a shell without the LLM path: no noise
        QVERIFY(!formatBackendInfo(m_r).contains(QStringLiteral("reasoning:")));
    }

    void everyAttemptIsListedWithItsReason()
    {
        m_r.hotkeyAttempts = {
            {QStringLiteral("xi2"), QStringLiteral("unavailable on wayland")},
            {QStringLiteral("portal-global-shortcuts"),
             QStringLiteral("portal.Error.NotAllowed: an app id is required")},
            {QStringLiteral("evdev"), QStringLiteral("chosen")},
        };
        const QString s = formatBackendInfo(m_r);
        QVERIFY(s.contains(QStringLiteral("xi2: unavailable on wayland")));
        QVERIFY(s.contains(QStringLiteral("portal.Error.NotAllowed")));
        QVERIFY(s.contains(QStringLiteral("evdev: chosen")));
    }

    void linesAreMachineGreppable()
    {
        // tests/e2e greps this output; keep one "key: value" per line.
        const QString s = formatBackendInfo(m_r);
        const QStringList lines = s.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            QVERIFY2(line.contains(QStringLiteral(": ")),
                     QStringLiteral("not a 'key: value' line: %1").arg(line).toUtf8());
        }
    }
};

QTEST_APPLESS_MAIN(TestBackendInfo)
#include "tst_backendinfo.moc"
