#include <QtTest>
#include "core/languages.h"

using namespace lt;

class TestLanguages : public QObject
{
    Q_OBJECT
private slots:
    void listIsSane()
    {
        const auto &l = languages();
        QVERIFY(l.size() >= 30);
        QSet<QString> codes;
        for (const auto &e : l) {
            QVERIFY(!e.code.isEmpty());
            QVERIFY(!e.name.isEmpty());
            QVERIFY(!codes.contains(e.code));   // no duplicate codes
            codes.insert(e.code);
        }
    }
    void lookupByName()
    {
        QCOMPARE(languageName(QStringLiteral("ru")), QString("Русский (Russian)"));
        QCOMPARE(languageName(QStringLiteral("zh-CN")).startsWith(QStringLiteral("简体")), true);
        QVERIFY(languageName(QStringLiteral("eo")).contains("Esperanto"));
    }
    void lookupMiss()
    {
        QVERIFY(languageName(QStringLiteral("xx")).isEmpty());
        QVERIFY(languageName(QString()).isEmpty());
    }
    void nameOrCodeFallback()
    {
        QCOMPARE(languageNameOrCode(QStringLiteral("fr")), QString("Français (French)"));
        QCOMPARE(languageNameOrCode(QStringLiteral("zz")), QString("zz"));
    }
};

QTEST_GUILESS_MAIN(TestLanguages)
#include "tst_languages.moc"
