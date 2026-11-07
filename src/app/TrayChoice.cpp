#include "TrayChoice.h"

namespace lt {

namespace {

QString normalise(const QString &value)
{
    return value.trimmed().toLower();
}

} // namespace

TrayChoice resolveTrayKind(const QString &buildDefault, const QString &envValue)
{
    TrayChoice c;
    const QString def = normalise(buildDefault);
    c.kind = (def == QLatin1String("qt") || def == QLatin1String("kde"))
        ? def : QStringLiteral("kde");
    c.source = QStringLiteral("built-in default");

    const QString wanted = normalise(envValue);
    if (wanted.isEmpty()) {
        return c;                       // no override requested
    }
    if (wanted != QLatin1String("qt") && wanted != QLatin1String("kde")) {
        // Loud, not silent: a typo would otherwise have the operator compare
        // the tray they did not ask for and conclude something false.
        c.warning = QStringLiteral("TRADUKU_TRAY='%1' is not understood "
                                   "(use 'qt' or 'kde'); keeping '%2' from %3")
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
    if (k == QLatin1String("kde")) {
        return QStringLiteral("kde (KStatusNotifierItem)");
    }
    if (k == QLatin1String("qt")) {
        return QStringLiteral("qt (QSystemTrayIcon)");
    }
    if (k == QLatin1String("none")) {
        return QStringLiteral("none (no tray host)");
    }
    // Never return an empty string: --backend-info is both read by humans and
    // grepped by the e2e suite, and a missing value reads as "fine".
    return QStringLiteral("unknown (%1)").arg(kind);
}

} // namespace lt
