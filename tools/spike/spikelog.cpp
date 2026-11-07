#include "spikelog.h"
#include <QTextStream>
namespace lt {
void say(const QString &line)      { QTextStream(stdout) << "[tray-spike] " << line << Qt::endl; }
void sayErr(const QString &line)   { QTextStream(stderr) << "[tray-spike] " << line << Qt::endl; }
} // namespace lt
