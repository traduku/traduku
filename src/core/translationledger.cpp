#include "translationledger.h"

#include <algorithm>

namespace lt {

namespace {
constexpr int MaxDiffCells = 250'000;   // LCS quadratic guard

bool isBlank(const QString &s)
{
    return std::all_of(s.cbegin(), s.cend(),
                       [](QChar c) { return c.isSpace(); });
}

QVector<TranslationLedger::Slot> chunkToSlots(const QVector<Chunk> &chunks)
{
    QVector<TranslationLedger::Slot> out;
    out.reserve(chunks.size());
    for (const Chunk &c : chunks) {
        TranslationLedger::Slot s;
        s.text = c.text;
        s.sep = c.sep;
        s.state = (c.text.isEmpty() || isBlank(c.text))
            ? TranslationLedger::Passthrough : TranslationLedger::Pending;
        if (s.state == TranslationLedger::Passthrough) {
            s.translation = c.text + c.sep;   // passthrough content
        }
        out.append(s);
    }
    return out;
}
} // namespace

void TranslationLedger::setSource(const QString &text, int maxChunkChars)
{
    m_slots = chunkToSlots(splitChunks(text, maxChunkChars));
    if (m_slots.isEmpty()) {
        m_slots.append(Slot{});   // empty document: one passthrough
        m_slots.last().state = Passthrough;
    }
}

QVector<int> TranslationLedger::applyEdit(const QString &text, int maxChunkChars)
{
    const QVector<Slot> fresh = chunkToSlots(splitChunks(text, maxChunkChars));

    QVector<int> dirty;
    const int n = m_slots.size();
    const int m = fresh.size();

    if (n * m > MaxDiffCells) {
        m_slots = fresh;          // huge document: full reset
    } else {
        // LCS on exact chunk text: matched pairs keep their state.
        QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
        for (int i = n - 1; i >= 0; --i) {
            for (int j = m - 1; j >= 0; --j) {
                if (m_slots.at(i).text == fresh.at(j).text) {
                    dp[i][j] = dp[i + 1][j + 1] + 1;
                } else {
                    dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
                }
            }
        }
        QVector<Slot> out;
        out.reserve(m);
        int i = 0, j = 0;
        while (i < n && j < m) {
            if (m_slots.at(i).text == fresh.at(j).text) {
                Slot s = fresh.at(j);
                const Slot &old = m_slots.at(i);
                if (old.state == Translated) {
                    s.state = Translated;
                    s.translation = old.translation;
                }   // Passthrough stays Passthrough; in-flight -> Pending
                out.append(s);
                ++i;
                ++j;
            } else if (dp[i + 1][j] >= dp[i][j + 1]) {
                ++i;              // old slot dropped (deleted line)
            } else {
                out.append(fresh.at(j));   // inserted/changed line
                ++j;
            }
        }
        while (j < m) {
            out.append(fresh.at(j++));
        }
        m_slots = out;
    }

    for (int k = 0; k < m_slots.size(); ++k) {
        if (m_slots.at(k).state == Pending) {
            dirty.append(k);
        }
    }
    return dirty;
}

void TranslationLedger::setState(int i, State s)
{
    Slot &slot = m_slots[i];
    slot.state = s;
    if (s == Translating) {
        slot.partial.clear();
    }
}

void TranslationLedger::setPartial(int i, const QString &streamed)
{
    Slot &slot = m_slots[i];
    slot.partial = streamed;
    if (slot.state == Pending) {
        slot.state = Translating;
    }
}

void TranslationLedger::setTranslation(int i, const QString &text,
                                       const QString &sep)
{
    Slot &slot = m_slots[i];
    if (!sep.isEmpty()) {
        slot.sep = sep;
    }
    slot.translation = text;
    slot.partial.clear();
    slot.state = Translated;
}

QString TranslationLedger::sourceText() const
{
    QString out;
    for (const Slot &s : m_slots) {
        out += s.text + s.sep;
    }
    return out;
}

QString TranslationLedger::targetText() const
{
    QString out;
    for (const Slot &s : m_slots) {   // translations carry their normalised
                                      // separators (setTranslation sep arg)
        switch (s.state) {
        case Translated:
            out += s.translation + s.sep;
            break;
        case Translating:
            out += (s.partial.isEmpty() ? s.text : s.partial) + s.sep;
            break;
        case Pending:
        case Passthrough:
            out += s.text + s.sep;
            break;
        }
    }
    // A separator after the final unit is presentation noise.
    const QString tail = m_slots.isEmpty() ? QString() : m_slots.last().sep;
    if (!tail.isEmpty() && out.endsWith(tail)) {
        out.chop(tail.size());
    }
    return out;
}

int TranslationLedger::nextPending() const
{
    for (int i = 0; i < m_slots.size(); ++i) {
        if (m_slots.at(i).state == Pending) {
            return i;
        }
    }
    return -1;
}

} // namespace lt
