#include "chunker.h"

#include <QRegularExpression>

#include <algorithm>

namespace lt {

namespace {

QVector<QString> splitSentences(const QString &block, int maxChars)
{
    QVector<QString> parts;
    static const QRegularExpression boundary(
        QStringLiteral("(?<=[.!?\\x2026]) +"));   // ". " "! " "? " "… "
    int partStart = 0;
    int lastBoundaryEnd = -1;   // end index of the last completed sentence
    auto flushTo = [&](int end) {
        parts.append(block.mid(partStart, end - partStart));
        partStart = end;
    };
    QRegularExpressionMatchIterator it = boundary.globalMatch(block);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int end = m.capturedEnd(0);
        if (end - partStart >= maxChars) {
            flushTo(lastBoundaryEnd > partStart ? lastBoundaryEnd : end);
        }
        lastBoundaryEnd = end;
    }
    // Remaining tail: split hard when it exceeds the cap.
    while (block.size() - partStart > maxChars) {
        flushTo(partStart + maxChars);
    }
    if (partStart < block.size()) {
        flushTo(block.size());
    }
    return parts;
}

} // namespace

QVector<Chunk> splitChunks(const QString &text, int maxChunkChars)
{
    QVector<Chunk> out;
    const int n = text.size();
    int i = 0;

    // Leading line breaks have no block of their own.
    if (n > 0 && text.at(0) == QLatin1Char('\n')) {
        int j = 0;
        while (j < n && text.at(j) == QLatin1Char('\n')) {
            ++j;
        }
        out.append({QString(), text.left(j)});
        i = j;
    }

    while (i < n) {
        int t = i;
        while (t < n && text.at(t) != QLatin1Char('\n')) {
            ++t;
        }
        const QString block = text.mid(i, t - i);
        int w = t;
        while (w < n && text.at(w) == QLatin1Char('\n')) {
            ++w;
        }
        QString sep = text.mid(t, w - t);

        // Whitespace-only line (spaces/tabs): part of the separators, not a
        // translation unit.
        if (block.isEmpty() || std::all_of(block.cbegin(), block.cend(),
                                           [](QChar ch) { return ch.isSpace(); })) {
            if (!out.isEmpty()) {
                out.last().sep += block + sep;
            } else {
                out.append({QString(), block + sep});
            }
            i = w;
            continue;
        }

        if (block.size() > maxChunkChars) {
            const QVector<QString> parts = splitSentences(block, maxChunkChars);
            for (int k = 0; k < parts.size(); ++k) {
                out.append({parts.at(k), k + 1 == parts.size() ? sep
                                                               : QString()});
            }
        } else {
            out.append({block, sep});
        }
        i = w;
    }
    return out;
}

} // namespace lt
