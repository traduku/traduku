#include "texttargets.h"

#include "unicodeescapes.h"

#include <QTextCodec>

namespace lt {
namespace TextTargets {

namespace {

QString charsetOf(const QString &type)
{
    const int semi = type.indexOf(QLatin1Char(';'));
    if (semi < 0) {
        return QString();
    }
    const QString param = type.mid(semi + 1).trimmed();
    if (!param.startsWith(QLatin1String("charset="), Qt::CaseInsensitive)) {
        return QString();
    }
    QString cs = param.mid(8).trimmed().toUpper();
    if (cs == QLatin1String("UTF8")) {
        cs = QStringLiteral("UTF-8");
    }
    return cs;
}

/// First offer whose lower-case form is in `wanted`, returned as offered.
QString firstOf(const QStringList &offered, const QStringList &wanted)
{
    for (const QString &want : wanted) {
        for (const QString &offer : offered) {
            if (offer.compare(want, Qt::CaseInsensitive) == 0) {
                return offer;
            }
        }
    }
    return QString();
}

bool looksLikeText(const QString &type)
{
    static const QStringList bookkeeping{
        QStringLiteral("TARGETS"), QStringLiteral("MULTIPLE"),
        QStringLiteral("SAVE_TARGETS"), QStringLiteral("TIMESTAMP"),
        QStringLiteral("INCR"), QStringLiteral("ATOM"),
        QStringLiteral("INTEGER"), QStringLiteral("ATOM"),
    };
    for (const QString &b : bookkeeping) {
        if (type.compare(b, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return type.startsWith(QLatin1String("text/"), Qt::CaseInsensitive)
           || type.compare(QLatin1String("UTF8_STRING"), Qt::CaseInsensitive) == 0
           || type.compare(QLatin1String("TEXT"), Qt::CaseInsensitive) == 0
           || type.compare(QLatin1String("STRING"), Qt::CaseInsensitive) == 0
           || type.compare(QLatin1String("COMPOUND_TEXT"), Qt::CaseInsensitive) == 0;
}

bool isValidUtf8(const QByteArray &bytes)
{
    // Round trip: fromUtf8 replaces every illegal sequence with U+FFFD, so
    // bytes that come back unchanged were legal UTF-8 to begin with.
    return QString::fromUtf8(bytes).toUtf8() == bytes;
}

QString fromCharset(const QByteArray &data, const QString &charset)
{
    if (charset.isEmpty() || charset == QLatin1String("UTF-8")) {
        return QString::fromUtf8(data);
    }
    if (charset == QLatin1String("US-ASCII") || charset == QLatin1String("ISO-8859-1")) {
        return QString::fromLatin1(data);
    }
    if (const QTextCodec *codec = QTextCodec::codecForName(charset.toLatin1())) {
        return codec->toUnicode(data);
    }
    return QString::fromUtf8(data);       // last resort: keep the bytes' story
}

} // namespace

QString pickBestMime(const QStringList &offered)
{
    // Order as measured off a real KDE clipboard: the explicit utf-8 text
    // offer first, then plain text, then the X11-style name some wayland
    // sources still advertise, then TEXT.
    static const QStringList exact{QStringLiteral("text/plain;charset=utf-8"),
                                   QStringLiteral("text/plain;charset=UTF-8"),
                                   QStringLiteral("text/plain"),
                                   QStringLiteral("UTF8_STRING")};
    const QString best = firstOf(offered, exact);
    if (!best.isEmpty()) {
        return best;
    }
    // Any charset-tagged text/plain (decoded per its charset), TEXT last.
    QString fallback;
    for (const QString &t : offered) {
        if (t.startsWith(QLatin1String("text/plain;charset="), Qt::CaseInsensitive)) {
            return t;
        }
        if (fallback.isEmpty() && t.compare(QLatin1String("TEXT"), Qt::CaseInsensitive) == 0) {
            fallback = t;
        }
    }
    return fallback;
}

QString pickBestAtom(const QStringList &targets)
{
    QStringList texty;
    texty.reserve(targets.size());
    for (const QString &t : targets) {
        if (looksLikeText(t)) {
            texty << t;
        }
    }
    if (texty.isEmpty()) {
        return QString();
    }
    // UTF8_STRING first: it is unambiguous, cheap, and the only target every
    // modern X client is required to serve for text. Then the MIME spellings
    // Qt and browsers offer, then the lossy legacy ones.
    static const QStringList exact{QStringLiteral("UTF8_STRING"),
                                   QStringLiteral("text/plain;charset=utf-8"),
                                   QStringLiteral("text/plain;charset=UTF-8"),
                                   QStringLiteral("text/plain;charset=utf8"),
                                   QStringLiteral("text/plain"),
                                   QStringLiteral("STRING")};
    const QString best = firstOf(texty, exact);
    if (!best.isEmpty()) {
        return best;
    }
    for (const QString &t : texty) {
        if (t.startsWith(QLatin1String("text/plain;charset="), Qt::CaseInsensitive)
            || t.startsWith(QLatin1String("text/"), Qt::CaseInsensitive)) {
            return t;
        }
    }
    for (const QString &t : texty) {
        if (t.compare(QLatin1String("TEXT"), Qt::CaseInsensitive) == 0
            || t.compare(QLatin1String("COMPOUND_TEXT"), Qt::CaseInsensitive) == 0) {
            return t;
        }
    }
    return QString();
}

QString decodeWithCharset(const QByteArray &data, const QString &mimeType)
{
    const QString cs = charsetOf(mimeType);
    // No charset parameter means US-ASCII by MIME's own default, which in
    // practice means the bytes are already UTF-8; fromUtf8 keeps both true.
    return cs.isEmpty() ? QString::fromUtf8(data) : fromCharset(data, cs);
}

QString decodeAtom(const QByteArray &data, const QString &atomName)
{
    const QString name = atomName.section(QLatin1Char(';'), 0, 0).trimmed().toUpper();
    if (name == QLatin1String("STRING")) {
        return QString::fromLatin1(data);            // ICCCM: ISO-8859-1
    }
    if (name == QLatin1String("TEXT") || name == QLatin1String("COMPOUND_TEXT")) {
        // The legacy offer carries no charset: the sending app's locale did.
        // Trust UTF-8 only when the bytes prove it, otherwise read the
        // windows-1251 that the measured garble was made of.
        if (isValidUtf8(data)) {
            return decodeIfEscaped(QString::fromUtf8(data));
        }
        if (const QTextCodec *cp = QTextCodec::codecForName("Windows-1251")) {
            return decodeIfEscaped(cp->toUnicode(data));
        }
        return decodeIfEscaped(QString::fromUtf8(data));
    }
    if (atomName.contains(QLatin1Char(';'))) {
        return decodeIfEscaped(decodeWithCharset(data, atomName));
    }
    // UTF8_STRING and anything else text-ish: UTF-8, with the escape net.
    return decodeIfEscaped(QString::fromUtf8(data));
}

} // namespace TextTargets
} // namespace lt
