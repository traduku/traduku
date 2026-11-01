#pragma once

#include "chunker.h"

#include <QString>
#include <QVector>

namespace lt {

/*!
 * Monotonic alignment of translated chunks against source chunks.
 *
 * Both sides are chunked with the same deterministic chunker; pairing is
 * then a sequence alignment problem scored by length ratio (log-scale, so
 * e.g. German expansion ~2x is normal, not a mismatch). Moves:
 *   match  src[i] <-> tgt[j]      cost = |ln(len_i/len_j)| / 2
 *   dropS  src[i] untranslated    cost 0.60   (result gets "")
 *   dropT  tgt[j] not consumed    cost 0.50   (text appended to the
 *                                              enclosing matched run)
 * Returns one string per source chunk (merged target runs join with their
 * separators; slots of dropped source chunks get ""). Returns an EMPTY
 * vector when the alignment quality is too low - the caller must then
 * fall back to per-chunk requests. Blank source slots pass through with
 * an empty result and are not part of the alignment.
 */
QVector<QString> alignChunks(const QVector<Chunk> &src, const QVector<Chunk> &tgt);

} // namespace lt
