#pragma once

#include <QString>

namespace lt {

/*!
 * Which tray the shell must create.
 *
 * Two inputs, deliberately kept apart:
 *   - the build default (LT_TRAY, set by CMake, so a distro package decides);
 *   - TRADUKU_TRAY, a per-run override, so a packaging default can be
 *     overridden in one command instead of a rebuild.
 *
 * There is only one tray implementation left - the QSystemTrayIcon one in
 * src/platform/qt/QtTray.cpp - so the choices are "qt" and "none". The KDE
 * variant (KStatusNotifierItem) is deleted: the Qt tray matched it entry for
 * entry in Plasma and the KF6 item segfaulted when destroyed with a live
 * session bus.
 *
 * The function still takes the build default and still warns, because
 * `-DLT_TRAY=kde` sits in old packaging recipes and `TRADUKU_TRAY=kde` in old
 * notes, and both must be answered out loud, not ignored.
 *
 * Resolution is pure (no environment access here) so
 * tests/unit/tst_traychoice covers the typo case.
 */
struct TrayChoice
{
    QString kind;        // "qt" | "none"
    QString source;      // where the decision came from, for --backend-info
    QString warning;     // non-empty = the request was ignored, say why
};

/// @param buildDefault  LT_TRAY for this build ("qt" or "none")
/// @param envValue      TRADUKU_TRAY, or empty when unset
TrayChoice resolveTrayKind(const QString &buildDefault, const QString &envValue);

/// What --backend-info prints: "qt (QSystemTrayIcon)", "none (no tray host)".
/// Never empty: the e2e suite greps this, and a blank reads as "fine".
QString trayDisplayName(const QString &kind);

} // namespace lt
