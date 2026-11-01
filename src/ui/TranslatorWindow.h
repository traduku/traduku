#pragma once

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTextEdit;
class QShortcut;

namespace lt {

/*!
 * DeepL-style interactive translation window: the left pane is an
 * editable source (paste with Ctrl+V, type freely), the right pane shows
 * the translation as a list of row slots so the engine can stream a new
 * answer into the MIDDLE of an already translated document.
 *
 * Rendering model (no state of its own beyond display):
 *   showTargetRows(rows)      - lay out the document (placeholders first)
 *   streamIntoRow(i, partial) - live-replace row i (neighbour rows intact)
 *   commitRow(i, text)        - finalise row i
 *
 * User interactions surface as signals: sourceEdited() (typing/paste,
 * never from setSourceText), translateRequested() (Ctrl+Enter),
 * copyRequested(), targetLanguagePicked(), settingsRequested().
 */
class TranslatorWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TranslatorWindow(QWidget *parent = nullptr);

    void setTargetLanguage(const QString &code);
    QString targetLanguage() const;

    QString sourceText() const;
    /// Programmatic fill (clipboard gesture) - does NOT emit sourceEdited.
    void setSourceText(const QString &text);

public slots:
    void statusTranslating(const QString &targetName);
    void statusDone();
    void statusFailed(const QString &reason);
    void showTargetRows(const QStringList &rows);
    void streamIntoRow(int row, const QString &partial);
    void commitRow(int row, const QString &text);
    void showForeground();

Q_SIGNALS:
    void sourceEdited();
    void translateRequested();
    void copyRequested(const QString &translation);
    void targetLanguagePicked(const QString &code);
    void settingsRequested();

private:
    void renderTarget();
    void updateCopyEnabled();

    QTextEdit *m_source;
    QTextEdit *m_target;
    QComboBox *m_language;
    QLabel *m_status;
    QPushButton *m_copy;
    QShortcut *m_translateShortcut;
    QVector<QString> m_rows;
    QVector<bool> m_committed;
    bool m_programmaticSource = false;
    bool m_programmaticLang = false;
};

} // namespace lt
