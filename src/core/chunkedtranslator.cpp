#include "chunkedtranslator.h"

#include "languages.h"
#include "llmstreamclient.h"
#include "settings.h"
#include "unicodeescapes.h"

namespace lt {

ChunkedTranslator::ChunkedTranslator(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QString ChunkedTranslator::rowText(int row) const
{
    const auto &slot = m_ledger.slot(row);
    switch (slot.state) {
    case TranslationLedger::Translated:
        return slot.translation;
    case TranslationLedger::Translating:
        return slot.partial.isEmpty() ? slot.text : slot.partial;
    default:
        return slot.text;
    }
}

QStringList ChunkedTranslator::rowLayout() const
{
    QStringList rows;
    for (int i = 0; i < m_ledger.slotCount(); ++i) {
        rows << rowText(i);
    }
    return rows;
}

bool ChunkedTranslator::isComplete() const
{
    return m_queue.isEmpty() && m_active.isEmpty() && m_ledger.isComplete();
}

void ChunkedTranslator::setSource(const QString &text)
{
    abortAll();
    m_ledger.setSource(text);
    for (int i = 0; i < m_ledger.slotCount(); ++i) {
        if (m_ledger.state(i) == TranslationLedger::Pending) {
            m_queue.enqueue(i);
        }
    }
    Q_EMIT startedDocument(text, languageName(m_settings->targetLanguage()));
    Q_EMIT rowLayoutChanged(rowLayout());
    startQueued();
}

void ChunkedTranslator::applyEdit(const QString &text)
{
    abortAll();
    const QVector<int> dirty = m_ledger.applyEdit(text);
    for (int row : dirty) {
        m_queue.enqueue(row);
    }
    Q_EMIT rowLayoutChanged(rowLayout());
    startQueued();
}

void ChunkedTranslator::startQueued()
{
    while (m_active.size() < m_maxParallel && !m_queue.isEmpty()) {
        startRow(m_queue.dequeue());
    }
    if (m_queue.isEmpty() && m_active.isEmpty() && m_ledger.isComplete()) {
        Q_EMIT allDone();
    }
}

void ChunkedTranslator::startRow(int row)
{
    m_ledger.setState(row, TranslationLedger::Translating);
    auto *engine = new TranslationEngine(m_settings, nullptr, this);
    m_active.append({engine, row});

    connect(engine, &TranslationEngine::delta, this, [this, engine, row]() {
        Q_EMIT rowStreaming(row, decodeIfEscaped(engine->translation()));
    });
    connect(engine, &TranslationEngine::finished, this,
            [this, engine, row](const QString &translation) {
        const QString cleaned = decodeIfEscaped(translation);
        m_ledger.setTranslation(row, cleaned.trimmed(),
                                m_ledger.slot(row).sep);
        Q_EMIT rowCommitted(row, cleaned.trimmed());
        for (int k = 0; k < m_active.size(); ++k) {
            if (m_active.at(k).engine == engine) {
                m_active.removeAt(k);
                break;
            }
        }
        engine->deleteLater();
        startQueued();
    });
    connect(engine, &TranslationEngine::failed, this,
            [this](const QString &reason) {
        abortAll();
        Q_EMIT failed(reason);
    });
    engine->translate(m_ledger.slot(row).text);
}

void ChunkedTranslator::abortAll()
{
    m_queue.clear();
    for (const Job &job : std::as_const(m_active)) {
        job.engine->cancel();   // suppresses failed()
        job.engine->deleteLater();
    }
    m_active.clear();
}

} // namespace lt
