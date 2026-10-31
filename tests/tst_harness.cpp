#include <QtTest>

// Proves the test harness: QtTest + CTest + offscreen widgets available.
class TestHarness : public QObject
{
    Q_OBJECT
private slots:
    void testRuns_data();
    void testRuns();
    void widgetsOffscreen();
};

void TestHarness::testRuns_data()
{
    QTest::addColumn<int>("value");
    QTest::newRow("one") << 1;
    QTest::newRow("two") << 2;
}

void TestHarness::testRuns()
{
    QFETCH(int, value);
    QVERIFY(value > 0);
}

void TestHarness::widgetsOffscreen()
{
    QWARN(QByteArray("Qt version: " QT_VERSION_STR).constData());
    QWidget w;
    w.resize(100, 50);
    QVERIFY(w.size().width() == 100);
}

QTEST_MAIN(TestHarness)
#include "tst_harness.moc"
