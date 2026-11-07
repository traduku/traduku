#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace lt {

/*!
 * What to ask a clipboard owner for, and how to read what came back.
 *
 * Two readers share this: `WlClipboard` (a wayland source offers MIME types
 * through wl-paste -l) and `X11Clipboard` (an X11 owner lists selection
 * TARGETS, which are atoms). The names differ, the rules do not: take the
 * most explicit UTF-8 text offer, fall back through the looser text offers,
 * and never decode by guessing.
 *
 * Living in core is the point - it is not platform code. Splitting it per
 * platform is how the two readers drifted apart before, and the wayland half
 * carries the scar tissue of a real bug (ROADMAP.md §2: a KDE clipboard
 * provider served windows-1251 quoted-printable, and "just decode UTF-8"
 * turned Cyrillic into mojibake). The `\uXXXX` safety net is applied here,
 * last, and only when the text is demonstrably double-encoded
 * (core/unicodeescapes) - so a translation quoting JSON survives.
 */
namespace TextTargets {

/// Best MIME name from a wl-paste style offer list. Empty = no text offered.
QString pickBestMime(const QStringList &offered);

/// Best X11 selection target from a TARGETS atom list (UTF8_STRING, TEXT,
/// STRING, text/plain[;charset=...]). Case-insensitive; returns the offer's
/// own spelling, because that string is what we ask the owner for.
/// Empty = nothing textual (TARGETS/TIMESTAMP/SAVE_TARGETS are bookkeeping).
QString pickBestAtom(const QStringList &targets);

/// Decode bytes announced under a MIME type with a charset parameter
/// (UTF-8 default; windows-1251 and friends via QTextCodec).
QString decodeWithCharset(const QByteArray &data, const QString &mimeType);

/// Decode bytes delivered under an X11 atom type: UTF8_STRING as UTF-8,
/// STRING as ISO-8859-1 (ICCCM), TEXT as UTF-8 when the bytes really are
/// UTF-8 and as windows-1251 when they are not - the legacy-offer case that
/// produced the measured garble - and MIME-named targets via the charset.
QString decodeAtom(const QByteArray &data, const QString &atomName);

} // namespace TextTargets
} // namespace lt
