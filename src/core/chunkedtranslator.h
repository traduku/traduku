#pragma once

#include "translationengine.h"
#include "translationledger.h"

#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QVector>

namespace lt {

class Settings;

/*!
 * Document-level incremental translation pipeline.
 *
 * Splits the source into translation units (core::chunker), tracks each
 * unit's state in a TranslationLedger, and runs units through a small
 * pool of TranslationEngines. On edit, the ledger's LCS diff marks only
 * touched units Pending - untouched translations are never re-requested.
 * In-flight jobs are cancelled on any relayout, so a mid-stream edit
 * converges to exactly the visible document.
 *
 * Rows are ledger slots: rowText()/rowLayout render them, rowStreaming
 * and rowCommitted drive the UI's live middle-of-document updates.
 */
class ChunkedTranslator : public QObject
{
    Q_OBJECT
public:
    explicit ChunkedTranslator(Settings *settings, QObject *parent = nullptr);

    void setSource(const QString &text);
    /// Re-chunk the (edited) document; only touched units are requeued.
    void applyEdit(const QString &text);

    QString targetText() const { return m_ledger.targetText(); }
    QString rowText(int row) const;
    QStringList rowLayout() const;
    bool isComplete() const;

Q_SIGNALS:
    void startedDocument(const QString &source, const QString &targetName);
    void rowLayoutChanged(const QStringList &rows);
    void rowStreaming(int row, const QString &partial);
    void rowCommitted(int row, const QString &translation);
    void allDone();
    void failed(const QString &reason);

private:
    struct Job {
        TranslationEngine *engine;
        int row;
    };
    void startQueued();
    void startRow(int row);
    void abortAll();

    TranslationLedger m_ledger;
    Settings *m_settings;
    QQueue<int> m_queue;
    QVector<Job> m_active;
    int m_maxParallel = 2;
};

} // namespace lt
