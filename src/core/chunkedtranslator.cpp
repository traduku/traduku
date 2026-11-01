#include "chunkedtranslator.h"

#include "aligner.h"
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
    QVector<int> map;
    for (int i = 0; i < m_ledger.slotCount(); ++i) {
        map.append(rows.size());
        rows << rowText(i);
        // A paragraph break in the separator renders as a blank row.
        if (m_ledger.slot(i).sep.contains(QStringLiteral("\n\n"))) {
            rows << QString();
        }
    }
    const_cast<ChunkedTranslator *>(this)->m_slotToRow = map;
    return rows;
}

int ChunkedTranslator::displayRow(int slot) const
{
    return slot >= 0 && slot < m_slotToRow.size() ? m_slotToRow.at(slot) : slot;
}

bool ChunkedTranslator::isComplete() const
{
    return !m_docEngine && m_queue.isEmpty() && m_active.isEmpty()
        && m_ledger.isComplete();
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
    const int budget = m_settings->maxWholeTextChars();
    const int contentUnits = m_ledger.slotCount() > 0
        ? int(m_queue.size()) : 0;
    if (budget > 0 && text.size() <= budget && contentUnits > 0) {
        startDocument();       // one request, full context, aligned locally
    } else {
        startQueued();
    }
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
    const int cap = qMax(1, m_settings->maxParallel());
    while (m_active.size() < cap && !m_queue.isEmpty()) {
        startRow(m_queue.dequeue());
    }
    if (m_queue.isEmpty() && m_active.isEmpty() && !m_docEngine
        && m_ledger.isComplete()) {
        Q_EMIT allDone();
    }
}

void ChunkedTranslator::startRow(int row)
{
    m_ledger.setState(row, TranslationLedger::Translating);
    auto *engine = new TranslationEngine(m_settings, nullptr, this);
    m_active.append({engine, row});

    connect(engine, &TranslationEngine::delta, this, [this, engine, row]() {
        Q_EMIT rowStreaming(displayRow(row), decodeIfEscaped(engine->translation()));
    });
    connect(engine, &TranslationEngine::finished, this,
            [this, engine, row](const QString &translation) {
        const QString cleaned = decodeIfEscaped(translation);
        m_ledger.setTranslation(row, cleaned.trimmed(),
                                m_ledger.slot(row).sep);
        Q_EMIT rowCommitted(displayRow(row), cleaned.trimmed());
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

void ChunkedTranslator::startDocument()
{
    m_queue.clear();   // the document job covers every queued slot
    m_docEngine = new TranslationEngine(m_settings, nullptr, this);
    m_docResult.clear();
    m_docTruncated = false;
    connect(m_docEngine, &TranslationEngine::delta, this, [this]() {
        m_docResult = m_docEngine->translation();
        // Live preview: chunk the streamed prefix with the same chunker and
        // feed positional partials; the final alignment refines boundaries.
        const QVector<Chunk> tc = splitChunks(m_docResult);
        int content = 0;
        for (int i = 0; i < m_ledger.slotCount() && content < tc.size(); ++i) {
            const QString txt = tc.at(content).text;
            if (txt.trimmed().isEmpty()) {
                ++content;
                continue;
            }
            if (m_ledger.state(i) == TranslationLedger::Pending
                || m_ledger.state(i) == TranslationLedger::Translating) {
                m_ledger.setPartial(i, txt);
                Q_EMIT rowStreaming(displayRow(i), txt);
            }
            ++content;
        }
    });
    connect(m_docEngine, &TranslationEngine::truncated, this,
            [this]() { m_docTruncated = true; });
    connect(m_docEngine, &TranslationEngine::finished, this,
            [this](const QString &translation) {
        finalizeDocument(translation);
    });
    connect(m_docEngine, &TranslationEngine::failed, this,
            [this](const QString &reason) {
        m_docEngine->deleteLater();
        m_docEngine = nullptr;
        Q_EMIT failed(reason);
    });
    m_docEngine->translate(m_ledger.sourceText().trimmed());
}

void ChunkedTranslator::finalizeDocument(const QString &result)
{
    m_docEngine->deleteLater();
    m_docEngine = nullptr;

    QVector<Chunk> src;
    for (int i = 0; i < m_ledger.slotCount(); ++i) {
        src.append({m_ledger.slot(i).text, m_ledger.slot(i).sep});
    }
    const QVector<QString> aligned
        = alignChunks(src, splitChunks(decodeIfEscaped(result).trimmed()));

    if (aligned.isEmpty()) {
        // Alignment refused (structure mismatch): chunk-mode fallback for
        // every content slot, previews discarded.
        m_queue.clear();
        for (int i = 0; i < m_ledger.slotCount(); ++i) {
            if (m_ledger.state(i) == TranslationLedger::Translating) {
                m_ledger.setState(i, TranslationLedger::Pending);
            }
            if (m_ledger.state(i) == TranslationLedger::Pending) {
                m_queue.enqueue(i);
            }
        }
        Q_EMIT rowLayoutChanged(rowLayout());
        startQueued();
        return;
    }

    bool tail = false;
    for (int i = 0; i < m_ledger.slotCount(); ++i) {
        const bool content = !m_ledger.slot(i).text.trimmed().isEmpty();
        if (!content) {
            continue;
        }
        const QString tr = aligned.at(i);
        if (!tail && !tr.isEmpty()) {
            m_ledger.setTranslation(i, tr, m_ledger.slot(i).sep);
            Q_EMIT rowCommitted(displayRow(i), tr);
        } else if (!tail && m_docTruncated) {
            tail = true;   // from here on: untranslated tail -> chunk jobs
            m_ledger.setState(i, TranslationLedger::Pending);
            m_queue.enqueue(i);
        } else if (tail) {
            m_ledger.setState(i, TranslationLedger::Pending);
            m_queue.enqueue(i);
        } else {
            m_ledger.setTranslation(i, QString(), m_ledger.slot(i).sep);
            Q_EMIT rowCommitted(displayRow(i), QString());
        }
    }
    Q_EMIT rowLayoutChanged(rowLayout());
    startQueued();
}

void ChunkedTranslator::abortAll()
{
    if (m_docEngine) {
        m_docEngine->cancel();
        m_docEngine->deleteLater();
        m_docEngine = nullptr;
    }
    m_queue.clear();
    for (const Job &job : std::as_const(m_active)) {
        job.engine->cancel();   // suppresses failed()
        job.engine->deleteLater();
    }
    m_active.clear();
}

} // namespace lt
