#pragma once

#include <QString>

namespace lt {

/*!
 * Which tray implementation the shell must create.
 *
 * Two inputs, deliberately kept apart:
 *   - the build default (LT_TRAY, set by CMake, so a distro package decides);
 *   - TRADUKU_TRAY, a per-run override, so comparing the two trays in a live
 *     session is one command instead of a rebuild - "did you remember to
 *     rebuild the right one?" is not a measurement.
 *
 * Resolution is pure (no environment access here) so tests/unit/tst_traychoice
 * covers the typo case, which is the one that would silently compare the
 * wrong tray.
 */
struct TrayChoice
{
    QString kind;        // "kde" | "qt"
    QString source;      // where the decision came from, for --backend-info
    QString warning;     // non-empty = the request was ignored, say why
};

/// @param buildDefault  LT_TRAY for this build ("kde" or "qt")
/// @param envValue      TRADUKU_TRAY, or empty when unset
TrayChoice resolveTrayKind(const QString &buildDefault, const QString &envValue);

/// What --backend-info prints: "kde (KStatusNotifierItem)",
/// "qt (QSystemTrayIcon)", "none (no tray host)". Never empty.
QString trayDisplayName(const QString &kind);

} // namespace lt
