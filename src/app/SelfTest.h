#pragma once

#include <QString>

class QTextStream;

namespace lt {

/*!
 * Built-in smoke test (app --selftest): exercises every core module
 * in-process, headless (offscreen-safe), on temporary files. Checks:
 * settings round-trip, language table, double-press detection, SSE
 * streaming parse, prompt building, the result window, clipboard backend
 * selection and XDG autostart. With doLive=true it additionally performs
 * one translation against the configured endpoint (requires a reachable
 * OpenAI-compatible server).
 *
 * Returns the number of failed checks (0 = all good).
 */
int runSelfTest(QTextStream &out, bool doLive,
               const QString &configPath = QString());

} // namespace lt
