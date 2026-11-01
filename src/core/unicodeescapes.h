#pragma once

#include <QString>

namespace lt {

/*!
 * Safety net for clipboard/stream text that arrives with *literal*
 * `\uXXXX` JSON escape sequences - a known wayland/X11 quirk of cp1251
 * web pages going through some clipboard providers (Klipper history,
 * browser PRIMARY selections). This is NOT an encoding detector: proper
 * channels deliver real Unicode (and WlClipboard now negotiates the
 * charset-tagged MIME target itself). We only un-escape text that is
 * demonstrably double-encoded, decided by looksEscaped():
 *
 *   at least 2 valid  \\u[0-9a-fA-F]{4}  sequences
 *   covering >= 25%   of the string's characters
 *
 * so prose/code samples that merely *mention* an escape are untouched.
 */

/// Decode valid \\uXXXX (incl. surrogate pairs) and the classic
/// \\n \\r \\t \\" \\\\ \\/ escapes; leave invalid sequences verbatim.
QString decodeEscapes(const QString &text);

/// Heuristic gate described above.
bool looksEscaped(const QString &text);

/// decodeEscapes() applied only when looksEscaped() says so.
QString decodeIfEscaped(const QString &text);

} // namespace lt
