#pragma once

namespace lt {

/// RECORD key emitter: a *child process* mode of the app.
///
/// libXtst's XRecordEnableContext cannot be interrupted from the same
/// process (the disable request is never flushed while the read loop holds
/// the display lock, and the async API never dispatches on Xwayland), so the
/// passive X11 listener runs here instead: it prints one line per key event
/// to stdout and is killed when the parent stops listening.
///
/// Output format: "<K|R> <keysym> <keycode>\n" (K = press, R = release).
/// Never returns: the child lives until the parent kills it.
int runRecordKeyEmitter();

} // namespace lt
