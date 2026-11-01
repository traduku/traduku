#pragma once

#include <QObject>
#include <QString>

namespace lt {

class Settings;
class LlmStreamClient;

/*!
 * One translation "job" per activation: builds the marker-wrapped prompt,
 * fires the streaming client and re-emits the lifecycle:
 *   started(source, targetLanguageName) -> delta(...)* -> finished(text)
 *                                              \\-> failed(reason)
 * finished carries the full concatenation of all deltas. A second
 * translate() while busy is ignored (single gesture at a time).
 */
class TranslationEngine : public QObject
{
    Q_OBJECT
public:
    explicit TranslationEngine(Settings *settings,
                               LlmStreamClient *client = nullptr,
                               QObject *parent = nullptr);

    /// The prompt sent to the model (exposed for tests / debugging).
    static QString buildPrompt(const QString &text, const QString &targetName);

    void translate(const QString &text);
    bool isBusy() const { return m_busy; }

    QString sourceText() const { return m_source; }
    QString translation() const { return m_result; }
    void cancel();   // abort in-flight request, suppress the failed() signal

Q_SIGNALS:
    void started(const QString &sourceText, const QString &targetLanguageName);
    void delta(const QString &chunk);
    void finished(const QString &translation);
    void failed(const QString &reason);

private:
    Settings *m_settings = nullptr;
    LlmStreamClient *m_client = nullptr;
    bool m_ownsClient = false;
    bool m_busy = false;
    bool m_cancelled = false;
    QString m_source;
    QString m_result;
};

} // namespace lt
