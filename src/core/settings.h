#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace lt {

/*!
 * Application settings stored as a plain INI file (portable across DEs:
 * no KConfig, no QSettings cache quirks - we control the bytes).
 * Default location: $XDG_CONFIG_HOME/traduku/config.ini
 *
 * Format: UTF-8, `[General]` section, `Key=Value` lines, `;`/`#` comments.
 * Setters stage values; save() writes atomically and emits changed().
 * reload() re-reads the file and drops unsaved edits.
 */
class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr);
    explicit Settings(const QString &filePath, QObject *parent = nullptr);

    static QString defaultFilePath();
    /// Default target language for a system locale name (testable seam:
    /// targetLanguage() feeds it QLocale::system().name()).
    static QString defaultTargetLanguageForLocale(const QString &localeName);
    QString filePath() const;

    QString apiUrl() const;             // base of an OpenAI-compatible API
    void setApiUrl(const QString &url);
    QString apiKey() const;             // Bearer token, may be empty
    void setApiKey(const QString &key);
    QString model() const;
    void setModel(const QString &model);
    QString targetLanguage() const;     // code from languages()
    void setTargetLanguage(const QString &code);
    double pressInterval() const;       // seconds between the two Ctrl+C
    void setPressInterval(double seconds);
    bool copyResult() const;            // clipboard := translation
    void setCopyResult(bool on);
    bool soundsEnabled() const;
    void setSoundsEnabled(bool on);
    int maxWholeTextChars() const;       // <= this: one whole-text request
    void setMaxWholeTextChars(int chars);
    int maxParallel() const;             // concurrent chunk requests
    void setMaxParallel(int n);

    /// Server-specific request options. Most current models are reasoning
    /// models: they stream a hidden "reasoning" channel we throw away, so
    /// asking the server to skip it is pure latency saved - measured on the
    /// live endpoint as 37 reasoning events / 0.71 s -> 0.21 s, and the answer
    /// stopped starting with the chat template's blank lines.
    bool disableReasoning() const;                    // default true
    void setDisableReasoning(bool on);
    QString reasoningOffOptions() const;              // JSON object, the preset
    void setReasoningOffOptions(const QString &json);
    QString extraOptions() const;                     // JSON object, merged last
    void setExtraOptions(const QString &json);
    static QString defaultReasoningOffOptions();
    bool autoTranslate() const;          // translate on source edits
    void setAutoTranslate(bool on);
    double autoTranslateDelay() const;   // s of idle typing before firing
    void setAutoTranslateDelay(double seconds);
    QString globalShortcut() const;     // fallback shortcut (Qt sequence)
    void setGlobalShortcut(const QString &seq);
    bool useInputCapture() const;       // Wayland portal opt-in
    void setUseInputCapture(bool on);
    bool portalKnownBroken() const;
    void setPortalKnownBroken(bool on);
    bool firstRunDone() const;
    void setFirstRunDone(bool on);

    bool save();
    /// True once the config file exists on disk - first-run detection.
    bool configFileExists() const;
    void reload();

Q_SIGNALS:
    void changed();

private:
    QVariant value(const char *key, const QVariant &fallback) const;
    void setValue(const char *key, const QVariant &v);

    QString m_path;
    bool m_existed = false;
    QVariantMap m_values;   // flat "General.Key" -> string
};

} // namespace lt
