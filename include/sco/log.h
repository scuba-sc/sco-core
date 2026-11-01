#pragma once
// Where sco-core's log lines go. The host (sc-offline, a test tool) installs a sink once at
// startup; until then lines are dropped.

namespace sco {

using LogSink = void (*)(const char* line);
// After a reserved host session takes ownership, legacy logging is refused:
// these void functions return without changing the sink or invoking it.
void SetLogSink(LogSink sink);
void Log(const char* fmt, ...);

}  // namespace sco
