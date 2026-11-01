#include "unicodeescapes.h"

namespace lt {

namespace {
int hexVal(QChar c)
{
    if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) {
        return c.unicode() - QLatin1Char('0').unicode();
    }
    if (c >= QLatin1Char('a') && c <= QLatin1Char('f')) {
        return 10 + c.unicode() - QLatin1Char('a').unicode();
    }
    if (c >= QLatin1Char('A') && c <= QLatin1Char('F')) {
        return 10 + c.unicode() - QLatin1Char('A').unicode();
    }
    return -1;
}

// "\\uXXXX" at position i -> code unit, -1 when malformed/incomplete.
int parseHex4(const QString &s, int i)
{
    int v = 0;
    for (int k = 0; k < 4; ++k) {
        const int d = hexVal(s.at(i + k));
        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

int countEscapes(const QString &s)
{
    int n = 0;
    for (int i = 0; i + 5 < s.size(); ++i) {
        if (s.at(i) == QLatin1Char('\\') && s.at(i + 1) == QLatin1Char('u')
            && parseHex4(s, i + 2) >= 0) {
            ++n;
            i += 5;
        }
    }
    return n;
}
} // namespace

QString decodeEscapes(const QString &text)
{
    QString out;
    out.reserve(text.size());
    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        if (c != QLatin1Char('\\') || i + 1 >= n) {
            out += c;
            ++i;
            continue;
        }
        const QChar next = text.at(i + 1);
        if (next == QLatin1Char('u') && i + 5 < n) {
            const int hi = parseHex4(text, i + 2);
            if (hi >= 0) {
                i += 6;
                if (hi >= 0xD800 && hi <= 0xDBFF && i + 5 < n
                    && text.at(i) == QLatin1Char('\\')
                    && text.at(i + 1) == QLatin1Char('u')) {
                    const int lo = parseHex4(text, i + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        out += QChar(ushort(hi));
                        out += QChar(ushort(lo));
                        i += 6;
                        continue;
                    }
                }
                if (hi >= 0xD800 && hi <= 0xDFFF) {
                    // unpaired surrogate: keep the original text verbatim
                    out += QStringLiteral("\\u");
                    out += text.mid(i - 4, 4);
                } else {
                    out += QChar(ushort(hi));
                }
                continue;
            }
        }
        switch (next.unicode()) {
        case 'n': out += QLatin1Char('\n'); i += 2; continue;
        case 'r': out += QLatin1Char('\r'); i += 2; continue;
        case 't': out += QLatin1Char('\t'); i += 2; continue;
        case '"': out += QLatin1Char('"');  i += 2; continue;
        case '\\': out += QLatin1Char('\\'); i += 2; continue;
        case '/': out += QLatin1Char('/');  i += 2; continue;
        default: break;
        }
        out += c;                    // not an escape: verbatim
        ++i;
    }
    return out;
}

bool looksEscaped(const QString &text)
{
    const int escapes = countEscapes(text);
    if (escapes < 2 || text.isEmpty()) {
        return false;
    }
    // A \\uXXXX sequence is 6 chars standing for one: escaped text is
    // massively inflated, so the 6*escapes/size ratio is the tell.
    return (6LL * escapes * 100) / text.size() >= 25;
}

QString decodeIfEscaped(const QString &text)
{
    return looksEscaped(text) ? decodeEscapes(text) : text;
}

} // namespace lt
