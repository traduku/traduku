#pragma once

#include "chunker.h"

#include <QString>
#include <QVector>

namespace lt {

/*!
 * Per-chunk translation state for the interactive window.
 *
 * setSource() starts a document (content slots Pending, whitespace slots
 * Passthrough). applyEdit() re-chunks the edited text and pairs chunks
 * with the previous slots by LCS on exact text: slots that survived keep
 * their translations untouched, changed/new slots become Pending - editing
 * one sentence therefore re-translates only that unit. Translations
 * stream in via setState/setPartial/setTranslation; targetText() renders
 * the live document (translations + placeholders for pending slots), so
 * a mid-document stream shows finished text on both sides.
 */
class TranslationLedger
{
public:
    enum State { Passthrough, Pending, Translating, Translated };

    struct Slot {
        QString text;          // source unit ("" for whitespace slots)
        QString sep;           // separator after the unit
        State state = Pending;
        QString translation;   // final (Translated)
        QString partial;       // streaming preview (Translating)
    };

    void setSource(const QString &text, int maxChunkChars = 1200);

    /// Re-chunk `text` and keep translations of unchanged chunks.
    /// Returns the indices of the newly-Pending slots (the work queue).
    QVector<int> applyEdit(const QString &text, int maxChunkChars = 1200);

    int slotCount() const { return m_slots.size(); }
    const Slot &slot(int i) const { return m_slots.at(i); }
    QString source(int i) const { return m_slots.at(i).text; }
    QString translation(int i) const { return m_slots.at(i).translation; }
    State state(int i) const { return m_slots.at(i).state; }
    const QVector<Slot> &all() const { return m_slots; }

    void setState(int i, State s);
    void setPartial(int i, const QString &streamed);
    /// Mark Translated; when sep is non-empty it overrides the slot
    /// separator (normalising what the model appended).
    void setTranslation(int i, const QString &text, const QString &sep = QString());

    QString sourceText() const;
    QString targetText() const;
    int nextPending() const;               // -1 when none
    bool isComplete() const { return nextPending() < 0; }

private:
    QVector<Slot> m_slots;
};

} // namespace lt
