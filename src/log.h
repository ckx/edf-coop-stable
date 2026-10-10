#pragma once

namespace dn {

// The transport part's log lines go to `sink` (one line, no timestamp, no line end): in EDF6Coop the game part's
// crash-safe log is the one log. Without a sink they are dropped (the tests).
using LogSink = void (*)(const char* line);
void logToSink(LogSink sink);
void logf(const char* fmt, ...);
// Same as logf, but at most once per `intervalMs` for the same key (for per-packet paths).
void logRateLimited(const char* key, unsigned intervalMs, const char* fmt, ...);

}  // namespace dn
