// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Private closed diagnostics v1 contract; no plugin ABI or generic JSON/invoke surface.
#include "sco/diagnostics.h"
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace sco::diagnostics::json {
struct Object {
    std::map<std::string, std::string> strings;
    std::map<std::string, uint64_t> integers;
    std::set<std::string> nulls;
};
// Flat scalar-only object. Max request payload 3992 bytes; no nested values are admitted
// (therefore the protocol's maximum depth 16 can never be reached). Clears out on failure.
bool Parse(std::string_view input, Object& out);
bool Control(uint16_t opcode, const Object& object);
bool Read(uint16_t opcode, const Object& object);
// Caller owns authentication, session admission and protected audit. Copies only; no refresh.
// Response <=65432 bytes (65536-byte frame minus 104-byte framing/MAC).
std::string Serialize(uint16_t opcode, const Object& object, SnapshotStore& snapshots,
                      Capture& capture, std::string_view session, uint64_t epoch, uint64_t now);
// Only fixed contract error codes; unknown codes become invalid_request. Unattested
// provenance is null, never copied from caller-supplied error text.
std::string Error(std::string_view code);
} // namespace sco::diagnostics::json
