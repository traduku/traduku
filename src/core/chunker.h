#pragma once

#include <QVector>
#include <QString>

namespace lt {

/// One translation unit: a text block (no line breaks inside) plus the
/// whitespace separator (line/paragraph break) that follows it.
/// The invariant join(split(x)) == x keeps reconstruction lossless.
struct Chunk {
    QString text;   // non-empty for translatable content, empty for
                    // whitespace-only documents
    QString sep;    // trailing whitespace ("\n", "\n\n", ...)
};

/// Split into translation units: line-based blocks; blocks longer than
/// maxChunkChars split at sentence boundaries ([.!?…] + space), and, when
/// none exist, at the cap.
QVector<Chunk> splitChunks(const QString &text, int maxChunkChars = 1200);

} // namespace lt
