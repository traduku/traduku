#pragma once
#include <QString>
namespace lt {
/// Everything the operator is supposed to read, on stdout, flushed line by
/// line: a spike whose evidence sits in a buffer nobody sees proves nothing.
void say(const QString &line);
void sayErr(const QString &line);
} // namespace lt
