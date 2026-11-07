#include "TrayChoice.h"

namespace lt {

namespace {

QString normalise(const QString &value)
{
    return value.trimmed().toLower();
}

bool isKnown(const QString &kind)
{
    return kind == QLatin1String("qt") || kind == QLatin1String("none");
}

} // namespace

TrayChoice resolveTrayKind(const QString &buildDefault, const QString &envValue)
{
    TrayChoice c;
    const QString def = normalise(buildDefault);
    // A build that asks for something impossible still gets a working app.
    c.kind = isKnown(def) ? def : QStringLiteral("qt");
    c.source = QStringLiteral("built-in default");

    const QString wanted = normalise(envValue);
    if (wanted.isEmpty()) {
        return c;                       // no override requested
    }
    if (wanted == QLatin1String("kde")) {
        // Old README, old muscle memory. Saying nothing here would let the
        // operator believe they were looking at KStatusNotifierItem.
        c.warning = QStringLiteral(
            "TRADUKU_TRAY='kde': the KDE (KStatusNotifierItem) tray was "
            "removed - both shells now use the Qt one, which registers with "
            "the same StatusNotifier host; keeping '%1'").arg(c.kind);
        return c;
    }
    if (!isKnown(wanted)) {
        // Loud, not silent: a typo would otherwise have the operator look at
        // the tray they did not ask for and conclude something false.
        c.warning = QStringLiteral("TRADUKU_TRAY='%1' is not understood "
                                   "(use 'qt' or 'none'); keeping '%2' from %3")
                        .arg(envValue, c.kind, c.source);
        return c;
    }
    c.kind = wanted;
    c.source = QStringLiteral("TRADUKU_TRAY");
    return c;
}

QString trayDisplayName(const QString &kind)
{
    const QString k = normalise(kind);
    if (k == QLatin1String("qt")) {
        return QStringLiteral("qt (QSystemTrayIcon)");
    }
    if (k == QLatin1String("none")) {
        return QStringLiteral("none (no tray host)");
    }
    if (k == QLatin1String("kde")) {
        // Reachable only from stale config; say what it became.
        return QStringLiteral("kde (removed; runs as QSystemTrayIcon)");
    }
    // Never return an empty string: --backend-info is both read by humans and
    // grepped by the e2e suite, and a missing value reads as "fine".
    return QStringLiteral("unknown (%1)").arg(kind);
}

} // namespace lt
