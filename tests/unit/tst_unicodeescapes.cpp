#include <QtTest>
#include "core/unicodeescapes.h"

using namespace lt;

class TestUnicodeEscapes : public QObject
{
    Q_OBJECT
private slots:
    void decodesBasicEscapes()
    {
        QCOMPARE(decodeEscapes(QStringLiteral("x \\u041f\\u0440\\u0438\\u0432\\u0435\\u0442 y")),
                 QStringLiteral("x Привет y"));
        QCOMPARE(decodeEscapes(QStringLiteral("a\\nb\\tc\\\\d\\\"e\\/f")),
                 QStringLiteral("a\nb\tc\\d\"e/f"));
    }

    void decodesSurrogatePairs()
    {
        QCOMPARE(decodeEscapes(QStringLiteral("hi \\uD83D\\uDE00!")),
                 QStringLiteral("hi \U0001F600!"));
    }

    void invalidSequencesKeptVerbatim()
    {
        QCOMPARE(decodeEscapes(QStringLiteral("\\u04")), QStringLiteral("\\u04"));
        QCOMPARE(decodeEscapes(QStringLiteral("\\uZZZZ")), QStringLiteral("\\uZZZZ"));
        QCOMPARE(decodeEscapes(QStringLiteral("tail\\")), QStringLiteral("tail\\"));
        QCOMPARE(decodeEscapes(QStringLiteral("\\x41")), QStringLiteral("\\x41"));
        // high surrogate without a following low one: kept as-is
        QCOMPARE(decodeEscapes(QStringLiteral("\\uD83Dx")), QStringLiteral("\\uD83Dx"));
    }

    void looksEscapedHeuristic()
    {
        // Plain text: never escaped.
        QVERIFY(!looksEscaped(QStringLiteral("Для начала - отношение к предмету.")));
        // A code snippet quoting ONE escape: below the gate -> untouched.
        QVERIFY(!looksEscaped(QStringLiteral(
            "const euro = \"\\u20AC\"; // json sample of a long code line "
            "that continues for a while to keep coverage low")));
        // The reported case: every Cyrillic letter serialized.
        QVERIFY(looksEscaped(QStringLiteral(
            "\\u0414\\u043b\\u044f \\u043d\\u0430\\u0447\\u0430\\u043b\\u0430 - "
            "\\u043e\\u0442\\u043d\\u043e\\u0448\\u0435\\u043d\\u0438\\u0435")));
        QVERIFY(!looksEscaped(QString()));
    }

    void decodeIfEscapedGatesDecoding()
    {
        const QString escaped =
            QStringLiteral("\\u0414\\u043b\\u044f \\u043d\\u0430\\u0447\\u0430\\u043b\\u0430");
        QCOMPARE(decodeIfEscaped(escaped), QStringLiteral("Для начала"));
        // Not escaped -> byte-for-byte identical (code snippets stay safe).
        const QString code = QStringLiteral(
            "printf(\"\\u20AC\"); const char *json = \"{\\\"a\\\":1}\"; "
            "// long line to keep the escape coverage of this sample low");
        QCOMPARE(decodeIfEscaped(code), code);
    }

    void realWorldClipboardCase()
    {
        // Exactly what the user saw in the Original pane (literal backslash-u).
        const QString s = QStringLiteral(
            "\\u0414\\u043b\\u044f \\u043d\\u0430\\u0447\\u0430\\u043b\\u0430 - "
            "\\u043e\\u0442\\u043d\\u043e\\u0448\\u0435\\u043d\\u0438\\u0435 \\u043a "
            "\\u043f\\u0440\\u0435\\u0434\\u043c\\u0435\\u0442\\u0443 \\u0438 "
            "\\u0430\\u0432\\u0442\\u043e\\u0440\\u0441\\u043a\\u043e\\u0435 "
            "\\u0441\\u0430\\u043c\\u043e\\u043e\\u043f\\u0440\\u0435\\u0434\\u0435\\u043b\\u0435\\u043d\\u0438\\u0435.");
        QVERIFY(looksEscaped(s));
        const QString d = decodeIfEscaped(s);
        QVERIFY(d.startsWith(QStringLiteral("Для начала - отношение")));
        QVERIFY(d.endsWith(QStringLiteral("самоопределение.")));
    }
};

QTEST_GUILESS_MAIN(TestUnicodeEscapes)
#include "tst_unicodeescapes.moc"
