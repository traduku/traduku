// Shared target/charset negotiation for clipboard readers.
//
// This is the code that decides WHICH representation of the clipboard we ask
// for and HOW we decode it, and it is deliberately not platform code: the
// Wayland reader (wl-paste offers MIME types) and the X11 reader (selection
// TARGETS are atoms) hit the same two traps and had, until here, two answers.
//
// Both traps are measurements from ROADMAP.md §2, not folklore:
//  - a KDE clipboard daemon serialises a rich document as windows-1251
//    quoted-printable and offers it under a charset-tagged text/plain;
//  - some providers hand over literal \uXXXX runs instead of UTF-8 bytes.
// Read the offered type, honour its charset, and only then apply the escape
// safety net - guessing the encoding is what produced the "Korean garble".
#include <QtTest>

#include "core/texttargets.h"

using namespace lt;

class TestTextTargets : public QObject
{
    Q_OBJECT
private:
    /// "Привет" in windows-1251. Measured with iconv, not remembered: 0xD0
    /// is "Р" there while it is also the first byte of UTF-8 "П" - that
    /// collision is exactly why a charset-less TEXT offer needs a
    /// discriminator (isValidUtf8 round trip) instead of a guess.
    static constexpr const char *CP1251_PRIVET = "CFF0E8E2E5F2";

    /// The word every payload here is a spelling of.
    static QString word()
    {
        return QString::fromUtf8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
    }

private slots:
    // ---- MIME names, as wl-paste -l reports them (moved out of WlClipboard:
    // ---- the X11 reader needs the same order, so it cannot live there)
    void pickBestMimePrefersUtf8Plain()
    {
        QCOMPARE(TextTargets::pickBestMime({QStringLiteral("text/html"),
                                            QStringLiteral("text/plain;charset=utf-8"),
                                            QStringLiteral("UTF8_STRING")}),
                 QStringLiteral("text/plain;charset=utf-8"));
    }

    void pickBestMimeKeepsTheOldOrder()
    {
        QCOMPARE(TextTargets::pickBestMime({QStringLiteral("TEXT"),
                                            QStringLiteral("text/plain")}),
                 QStringLiteral("text/plain"));
        QCOMPARE(TextTargets::pickBestMime({QStringLiteral("TEXT"),
                                            QStringLiteral("UTF8_STRING")}),
                 QStringLiteral("UTF8_STRING"));
        // charset-tagged but not utf-8: acceptable, and decoded per charset
        QCOMPARE(TextTargets::pickBestMime({QStringLiteral("TEXT"),
                                            QStringLiteral("text/plain;charset=windows-1251")}),
                 QStringLiteral("text/plain;charset=windows-1251"));
        // TEXT is the last resort among texts
        QCOMPARE(TextTargets::pickBestMime({QStringLiteral("image/png"),
                                            QStringLiteral("TEXT")}),
                 QStringLiteral("TEXT"));
    }

    void pickBestMimeSaysNoWhenThereIsNoText()
    {
        QVERIFY(TextTargets::pickBestMime({QStringLiteral("image/png"),
                                           QStringLiteral("application/octet-stream")}).isEmpty());
        QVERIFY(TextTargets::pickBestMime({}).isEmpty());
    }

    // ---- X11 selection TARGETS: atom names, not MIME types
    void pickBestAtomKnowsTheX11Names()
    {
        // A legacy Motif offer: UTF8_STRING present => take it, never TEXT.
        QCOMPARE(TextTargets::pickBestAtom({QStringLiteral("TIMESTAMP"),
                                            QStringLiteral("TEXT"),
                                            QStringLiteral("UTF8_STRING"),
                                            QStringLiteral("SAVE_TARGETS")}),
                 QStringLiteral("UTF8_STRING"));
        // Modern apps that offer MIME types on X11 too (Qt, Chrome).
        QCOMPARE(TextTargets::pickBestAtom({QStringLiteral("text/html"),
                                            QStringLiteral("text/plain;charset=utf-8")}),
                 QStringLiteral("text/plain;charset=utf-8"));
        QCOMPARE(TextTargets::pickBestAtom({QStringLiteral("text/html"),
                                            QStringLiteral("text/plain")}),
                 QStringLiteral("text/plain"));
        // ICCCM STRING is ISO-8859-1: below UTF8_STRING, above nothing.
        QCOMPARE(TextTargets::pickBestAtom({QStringLiteral("STRING")}),
                 QStringLiteral("STRING"));
        // Atoms are matched case-insensitively (charset=utf8 == utf-8).
        QCOMPARE(TextTargets::pickBestAtom({QStringLiteral("utf8_string")}),
                 QStringLiteral("utf8_string"));
        QCOMPARE(TextTargets::pickBestAtom({QStringLiteral("text/plain;charset=utf8")}),
                 QStringLiteral("text/plain;charset=utf8"));
    }

    void pickBestAtomIgnoresNonTextAndReturnsEmpty()
    {
        QVERIFY(TextTargets::pickBestAtom({QStringLiteral("image/png"),
                                           QStringLiteral("application/x-moz-url")}).isEmpty());
        QVERIFY(TextTargets::pickBestAtom({}).isEmpty());
        // TIMESTAMP and SAVE_TARGETS travel with TEXT but are not content.
        QVERIFY(TextTargets::pickBestAtom({QStringLiteral("TIMESTAMP"),
                                           QStringLiteral("TARGETS"),
                                           QStringLiteral("MULTIPLE"),
                                           QStringLiteral("SAVE_TARGETS")}).isEmpty());
    }

    // ---- decoding
    void decodeWithCharsetHonoursTheParameter()
    {
        // "Привет" spelled out per encoding, with fromHex rather than \xNN:
        // a C++ hex escape eats every following hex digit, so
        // "\xD0\x9F..." is one byte D09F..., and the first version of this
        // test asserted a mojibake expectation against a real decoder.
        const QByteArray wordUtf8 = word().toUtf8();
        const QByteArray cp1251 = QByteArray::fromHex(CP1251_PRIVET);
        Q_UNUSED(wordUtf8);
        QCOMPARE(TextTargets::decodeWithCharset(cp1251,
                 QStringLiteral("text/plain;charset=windows-1251")), word());
        QCOMPARE(TextTargets::decodeWithCharset(QStringLiteral("plain").toUtf8(),
                 QStringLiteral("text/plain")),
                 QStringLiteral("plain"));
        QCOMPARE(TextTargets::decodeWithCharset(QByteArray("abc"),
                 QStringLiteral("text/plain;charset=us-ascii")),
                 QStringLiteral("abc"));
        // An unknown charset must not throw away the bytes silently: utf-8
        // with replacement characters is the documented last resort.
        QCOMPARE(TextTargets::decodeWithCharset(QByteArray("\xFF\xFE"),
                 QStringLiteral("text/plain;charset=not-a-charset")),
                 QString::fromUtf8("\xFF\xFE"));
    }

    void decodeAtomFollowsThePropertyType()
    {
        QCOMPARE(TextTargets::decodeAtom(word().toUtf8(), QStringLiteral("UTF8_STRING")),
                 word());
        // ICCCM: STRING is latin-1, so the Cyrillic bytes are NOT latin-1
        // and a server that answers STRING with them is broken - but TEXT
        // below is the case that matters.
        QCOMPARE(TextTargets::decodeAtom(QByteArray("caf\xE9"), QStringLiteral("STRING")),
                 QStringLiteral("caf\xE9"));   // U+00E9, not U+FFFD
    }

    void decodeAtomRescuesTextLegacyOffers()
    {
        // The measured garble: an X11 owner offers only TEXT, and its bytes
        // are windows-1251 (locale of the sending app). Valid UTF-8 wins over
        // the guess; invalid UTF-8 must not turn into U+FFFD soup.
        QCOMPARE(TextTargets::decodeAtom(word().toUtf8(), QStringLiteral("TEXT")), word());
        const QByteArray cp1251 = QByteArray::fromHex(CP1251_PRIVET);   // NOT valid utf-8
        QVERIFY(!QString::fromUtf8(cp1251).toUtf8().isEmpty());
        QCOMPARE(TextTargets::decodeAtom(cp1251, QStringLiteral("TEXT")), word());
    }

    void decodeAtomAppliesTheEscapeSafetyNetOnlyWhenNeeded()
    {
        // A server handing over literal \uXXXX (measured with browser PRIMARY
        // selections) is rescued ...
        QCOMPARE(TextTargets::decodeAtom(
                     QByteArray("\\u041F\\u0440\\u0438\\u0432\\u0435\\u0442"),
                     QStringLiteral("UTF8_STRING")), word());
        // ... while a translation that quotes JSON survives untouched.
        const QByteArray json("{\"a\":\"\\u041F\"} more text here to dilute it");
        QCOMPARE(TextTargets::decodeAtom(json, QStringLiteral("UTF8_STRING")),
                 QString::fromLatin1(json));
    }
};

QTEST_GUILESS_MAIN(TestTextTargets)
#include "tst_texttargets.moc"
