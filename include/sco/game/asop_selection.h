// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Host-source seam: one preconfigured document; no caller-supplied paths.
#include <sco/runtime.h>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace sco::game::asop_selection {
enum class FileResult : uint32_t { Ok, NotFound, Busy, Error, Conflict, OutcomeUnknown, Cancelled, Invalid };
struct Document {
    bool present = false;
    std::string bytes;
    std::array<uint8_t, 32> identity{};
};
// Synchronous only: adapter must call begin immediately before its irreversible replacement.
// A false result forbids the replacement. The permit cannot be retained after the callback.
struct CommitPermit { void* context = nullptr; bool (*begin)(void*) = nullptr; };
struct FileAdapter {
    void* context = nullptr;
    FileResult (*read)(void*, Document&) = nullptr;
    FileResult (*replace)(void*, const Document&, std::string_view, Document&, const CommitPermit&) = nullptr;
};
struct Options {
    FileAdapter file;
    std::vector<std::string> catalogue;
    // Trailing additive members; null defaults keep the frozen behaviour. When set, the
    // callback is invoked on the game thread under this service's lock, so it may only
    // take locks ordered after it (the fleet lock). A busy class cannot be removed by a
    // committed save; the refusal is reported as EDIT_CONFLICT with REASON_BUSY.
    void* busy_context = nullptr;
    bool (*class_busy)(void* context, const char* class_name) = nullptr;
};
Result Start(const Options& options);
// Processes at most one accepted file job; never publishes an engine list.
Result Pump();
Result Stop();
} // namespace sco::game::asop_selection
