#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

namespace lt {

namespace {
constexpr double MinInterval = 0.1;
constexpr double MaxInterval = 3.0;
constexpr auto Section = "General";

QVariant parseValue(const QString &text)   // stored as string, typed on read
{
    return text;
}

QVariantMap parseIni(const QByteArray &raw)
{
    QVariantMap out;
    QString section = QStringLiteral("root");
    for (const QByteArray &lineRaw : raw.split('\n')) {
        const QString line = QString::fromUtf8(lineRaw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0) {
            out.insert(section + QLatin1Char('.') + line.left(eq).trimmed(),
                       parseValue(line.mid(eq + 1).trimmed()));
        }
    }
    return out;
}
} // namespace

Settings::Settings(QObject *parent)
    : Settings(defaultFilePath(), parent)
{
}

Settings::Settings(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath)
{
    reload();
}

QString Settings::defaultFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/llm-translate/config.ini");
}

QString Settings::filePath() const
{
    return m_path;
}

void Settings::reload()
{
    m_values.clear();
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly)) {
        m_values = parseIni(f.readAll());
    }
}

bool Settings::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QByteArray out;
    out += '[';
    out += Section;
    out += "]\n";
    const QString prefix = QLatin1String(Section) + QLatin1Char('.');
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        if (it.key().startsWith(prefix)) {
            out += it.key().mid(prefix.size()).toUtf8() + '='
                 + it.value().toString().toUtf8() + '\n';
        }
    }
    const QString tmp = m_path + QStringLiteral(".tmp");
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        if (f.write(out) != out.size()) {
            return false;
        }
    }
    QFile::remove(m_path);                       // rename over symlink-safe
    return QFile::rename(tmp, m_path);
}

QVariant Settings::value(const char *key, const QVariant &fallback) const
{
    return m_values.value(QLatin1String(Section) + QLatin1Char('.')
                          + QLatin1String(key), fallback);
}

void Settings::setValue(const char *key, const QVariant &v)
{
    m_values.insert(QLatin1String(Section) + QLatin1Char('.') + QLatin1String(key),
                    v.toString());
    Q_EMIT changed();
}

// Staged write + changed() only on an actual change.
#define LT_DEFINE_SETTER(Name, getter, Type, key)                 \
    void Settings::Name(Type v)                                   \
    {                                                             \
        if (getter() == v) {                                      \
            return;                                               \
        }                                                         \
        setValue(key, v);                                         \
    }

QString Settings::apiUrl() const
{
    return value("ApiUrl", QStringLiteral("http://localhost:11434/v1")).toString();
}
LT_DEFINE_SETTER(setApiUrl, apiUrl, const QString &, "ApiUrl")

QString Settings::apiKey() const
{
    return value("ApiKey", QString()).toString();
}
LT_DEFINE_SETTER(setApiKey, apiKey, const QString &, "ApiKey")

QString Settings::model() const
{
    return value("Model", QStringLiteral("llama3.1")).toString();
}
LT_DEFINE_SETTER(setModel, model, const QString &, "Model")

QString Settings::targetLanguage() const
{
    return value("TargetLanguage", QStringLiteral("ru")).toString();
}
LT_DEFINE_SETTER(setTargetLanguage, targetLanguage, const QString &, "TargetLanguage")

double Settings::pressInterval() const
{
    return value("PressInterval", 0.5).toDouble();
}
void Settings::setPressInterval(double seconds)
{
    const double v = std::clamp(seconds, MinInterval, MaxInterval);
    if (pressInterval() == v) {
        return;
    }
    setValue("PressInterval", v);
}

bool Settings::copyResult() const
{
    return value("CopyResult", true).toBool();
}
LT_DEFINE_SETTER(setCopyResult, copyResult, bool, "CopyResult")

bool Settings::autoTranslate() const
{
    return value("AutoTranslate", true).toBool();
}
LT_DEFINE_SETTER(setAutoTranslate, autoTranslate, bool, "AutoTranslate")

double Settings::autoTranslateDelay() const
{
    return value("AutoTranslateDelay", 0.6).toDouble();
}
void Settings::setAutoTranslateDelay(double seconds)
{
    const double v = std::clamp(seconds, 0.1, 5.0);
    if (autoTranslateDelay() == v) {
        return;
    }
    setValue("AutoTranslateDelay", v);
}

bool Settings::soundsEnabled() const
{
    return value("SoundsEnabled", true).toBool();
}
LT_DEFINE_SETTER(setSoundsEnabled, soundsEnabled, bool, "SoundsEnabled")

QString Settings::globalShortcut() const
{
    return value("GlobalShortcut", QStringLiteral("Ctrl+Alt+C")).toString();
}
LT_DEFINE_SETTER(setGlobalShortcut, globalShortcut, const QString &, "GlobalShortcut")

bool Settings::useInputCapture() const
{
    return value("UseInputCapture", false).toBool();
}
LT_DEFINE_SETTER(setUseInputCapture, useInputCapture, bool, "UseInputCapture")

bool Settings::portalKnownBroken() const
{
    return value("PortalKnownBroken", false).toBool();
}
LT_DEFINE_SETTER(setPortalKnownBroken, portalKnownBroken, bool, "PortalKnownBroken")

bool Settings::firstRunDone() const
{
    return value("FirstRunDone", false).toBool();
}
LT_DEFINE_SETTER(setFirstRunDone, firstRunDone, bool, "FirstRunDone")

} // namespace lt
