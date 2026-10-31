#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace lt {

/*!
 * Application settings stored as a plain INI file (portable across DEs:
 * no KConfig, no QSettings cache quirks - we control the bytes).
 * Default location: $XDG_CONFIG_HOME/llm-translate/config.ini
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
    QString globalShortcut() const;     // fallback shortcut (Qt sequence)
    void setGlobalShortcut(const QString &seq);
    bool useInputCapture() const;       // Wayland portal opt-in
    void setUseInputCapture(bool on);
    bool portalKnownBroken() const;
    void setPortalKnownBroken(bool on);
    bool firstRunDone() const;
    void setFirstRunDone(bool on);

    bool save();
    void reload();

Q_SIGNALS:
    void changed();

private:
    QVariant value(const char *key, const QVariant &fallback) const;
    void setValue(const char *key, const QVariant &v);

    QString m_path;
    QVariantMap m_values;   // flat "General.Key" -> string
};

} // namespace lt
