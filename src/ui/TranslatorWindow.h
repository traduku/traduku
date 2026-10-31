#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTextEdit;

namespace lt {

/*!
 * The DeepL-style result window: original text on the left, the live
 * translation stream on the right. A window (not a tray widget) because
 * translations can be long, need scrolling/selection and must work on
 * any desktop - KDE, GNOME, plain X11.
 *
 * Lifecycle driven by the app layer:
 *   beginTranslation(source, targetName) -> appendChunk()* ->
 *   endTranslation()  or  translationFailed(reason)
 * Clipboard / settings / language persistence are delegated to signals -
 * the window owns no state beyond what it displays.
 */
class TranslatorWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TranslatorWindow(QWidget *parent = nullptr);

    void setTargetLanguage(const QString &code);
    QString targetLanguage() const;

public slots:
    void beginTranslation(const QString &sourceText, const QString &targetName);
    void appendChunk(const QString &chunk);
    void endTranslation();
    void translationFailed(const QString &reason);
    /// Normal window flow: show + raise + focus (used on every activation).
    void showForeground();

Q_SIGNALS:
    void copyRequested(const QString &translation);
    void targetLanguagePicked(const QString &code);
    void settingsRequested();

private:
    QTextEdit *m_source;
    QTextEdit *m_target;
    QComboBox *m_language;
    QLabel *m_status;
    QPushButton *m_copy;
    bool m_programmatic = false;   // guard: setTargetLanguage() must not echo
    bool m_streaming = false;
    bool m_hasResult = false;
};

} // namespace lt
