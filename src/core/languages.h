#pragma once

#include <QString>
#include <QVector>

namespace lt {

struct Language {
    QString code;   // BCP-47-ish tag passed through to the prompt
    QString name;   // native + english, shown in pickers
};

/// Destination languages (the model auto-detects the source).
const QVector<Language> &languages();

/// Native name for a code, empty string when unknown.
QString languageName(const QString &code);

/// languageName() falling back to the raw code (never empty).
QString languageNameOrCode(const QString &code);

} // namespace lt
