#include "languages.h"
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
    // Snapshot BEFORE QSettings touches the disk (its first sync() creates
    // an empty file): the app uses this for first-run detection.
    m_existed = QFileInfo::exists(filePath);
    reload();
}

QString Settings::defaultTargetLanguageForLocale(const QString &localeName)
{
    return languageCodeForLocale(localeName);
}

QString Settings::defaultFilePath()
{
    // Two levels are enough for a handful of keys: an explicit --config,
    // then $TRADUKU_CONFIG, then the XDG default. The file is not moved to
    // ~/.traduku.ini - v0.2.0 users already have ~/.config/traduku/config.ini,
    // the settings dialog and the config watcher live there, and the same
    // directory also holds the autostart entry's siblings.
    const QString fromEnv = qEnvironmentVariable("TRADUKU_CONFIG");
    if (!fromEnv.isEmpty()) {
        return fromEnv;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/traduku/config.ini");
}

QString Settings::defaultReasoningOffOptions()
{
    // Both spellings, because servers disagree about the name: vLLM's Qwen
    // chat template reads chat_template_kwargs.enable_thinking, OpenAI-style
    // gateways take reasoning_effort. Verified against the live endpoint.
    // {"thinking":{"type":"disabled"}} is deliberately NOT used - on that same
    // server it silently produced an empty answer.
    return QStringLiteral(
        "{\"chat_template_kwargs\":{\"enable_thinking\":false},"
        "\"reasoning_effort\":\"none\"}");
}

bool Settings::disableReasoning() const
{
    return value("DisableReasoning", true).toBool();
}

void Settings::setDisableReasoning(bool on)
{
    setValue("DisableReasoning", on);
}

QString Settings::reasoningOffOptions() const
{
    return value("ReasoningOffOptions",
                 defaultReasoningOffOptions()).toString();
}

void Settings::setReasoningOffOptions(const QString &v)
{
    setValue("ReasoningOffOptions", v);
}

QString Settings::extraOptions() const
{
    return value("ExtraOptions", QString()).toString();
}

void Settings::setExtraOptions(const QString &v)
{
    setValue("ExtraOptions", v);
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
    migrateLegacySoundSwitch();
}

/// One old bit for two cues becomes two keys, once, on load.
///
/// The dialog used to have a single "starts / finishes" box writing
/// SoundsEnabled. Reading that file with the new controls means deciding what
/// `SoundsEnabled=false` says about each cue, and the answer cannot be "both
/// on": a user who muted the app would find sound back after an upgrade, which
/// is the one migration a user notices. A config with no SoundsEnabled key at
/// all is a new install, where both cues are on and nothing is written.
void Settings::migrateLegacySoundSwitch()
{
    const QString prefix = QLatin1String(Section) + QLatin1Char('.');
    const QString master = prefix + QLatin1String("SoundsEnabled");
    const QString start = prefix + QLatin1String("SoundsOnStart");
    const QString finish = prefix + QLatin1String("SoundsOnFinish");
    if (!m_values.contains(master) || m_values.contains(start)) {
        return;     // new config, or already carried over
    }
    const bool on = m_values.value(master).toBool();
    m_values.insert(start, on ? QStringLiteral("true") : QStringLiteral("false"));
    m_values.insert(finish, on ? QStringLiteral("true") : QStringLiteral("false"));
    // The master stops carrying meaning of its own (it becomes "at least one
    // cue is on", which the cue setters maintain) - leaving it false would
    // mute a cue the user has just seen checked in the dialog.
    m_values.insert(master, QStringLiteral("true"));
}

bool Settings::configFileExists() const
{
    return m_existed;
}

bool Settings::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    m_existed = true;
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
    return value("TargetLanguage",
                 defaultTargetLanguageForLocale(QLocale::system().name()))
        .toString();
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

int Settings::maxWholeTextChars() const
{
    return value("MaxWholeTextChars", 6000).toInt();
}
void Settings::setMaxWholeTextChars(int chars)
{
    const int v = std::clamp(chars, 0, 200000);
    if (maxWholeTextChars() == v) {
        return;
    }
    setValue("MaxWholeTextChars", v);
}

int Settings::maxParallel() const
{
    return value("MaxParallel", 4).toInt();
}
void Settings::setMaxParallel(int n)
{
    const int v = std::clamp(n, 1, 8);
    if (maxParallel() == v) {
        return;
    }
    setValue("MaxParallel", v);
}

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

void Settings::setSoundsEnabled(bool on)
{
    if (soundsEnabled() == on) {
        return;
    }
    setValue("SoundsEnabled", on);
    // One bit for two cues, in both directions. Muting the app mutes both,
    // because that is the only thing the old single box could mean - and
    // un-muting gives both back, because a master that silences two cues on the
    // way down and restores none on the way up is a switch that can only be
    // turned off. --selftest, the CLI and every config written before this
    // split use it alone, so it has to remain able to say "sound".
    setValue("SoundsOnStart", on);
    setValue("SoundsOnFinish", on);
}

bool Settings::soundOnStart() const
{
    return value("SoundsOnStart", true).toBool();
}

bool Settings::soundOnFinish() const
{
    // One switch for both endings: "the translation is over" is the moment the
    // user is asking about, and it arrives as a success or as a failure.
    return value("SoundsOnFinish", true).toBool();
}

void Settings::setSoundOnStart(bool on)
{
    if (soundOnStart() == on) {
        return;
    }
    setValue("SoundsOnStart", on);
    // The master is the pair, not a third switch somebody has to discover: a
    // user who muted both cues has a muted app - and --backend-info, which
    // answers with the master, must not claim a live sound path - while one
    // cue turned back on un-mutes it without a second click somewhere else.
    setValue("SoundsEnabled", on || soundOnFinish());
}

void Settings::setSoundOnFinish(bool on)
{
    if (soundOnFinish() == on) {
        return;
    }
    setValue("SoundsOnFinish", on);
    setValue("SoundsEnabled", on || soundOnStart());
}

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
