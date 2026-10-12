#pragma once
// Host-only C++ observation seam. Not part of the frozen sco_api plugin ABI.
#include "sco/runtime.h"
#include <optional>
#include <string>
#include <vector>

namespace sco::diagnostics {

struct ServiceMetadata {
    std::string service;
    uint32_t packedVersion;
    std::optional<std::string> ownerId;
    std::string ownerCategory;  // host, game_pack, plugin, unknown
    std::string status;         // published, withdrawn, admission_blocked
    bool exposed;              // owner-level hand-out history, not a consumer lease count
    std::optional<uint32_t> tableSize;
    std::optional<std::string> reason;
};

struct ServiceInventory {
    std::vector<ServiceMetadata> items;
    bool truncated = false;
};

// Any thread. Copies at most 1024 records under the registry lock, with live publications
// preferred over history. Owns all strings; no owner/table pointers, table reads, callbacks,
// QueryService calls or new hand-outs. Observation allocation loss is reported as truncated.
ServiceInventory CopyServiceMetadata();

// Trusted host registration only, for an already-published service owned by owner. Copies
// metadata, never reads the service table (even its size prefix). safeOwnerId may be null
// (unknown) or 1..31 [a-z0-9_]; category is exactly host/game_pack/plugin/unknown. tableSize
// may be unknown or 0..65536 bytes. Invalid metadata is BadArg; missing publication is
// NotFound; allocation failure is TooMany. No registry/admission/capability changes.
Result SetServiceDiagnosticMetadata(const void* owner, const char* name, const char* safeOwnerId,
                                    const char* category, std::optional<uint32_t> tableSize);

// Host-session observation reset: clears withdrawal history and its loss flag only. Does
// not clear live metadata, publications, hand-outs, reservations or released-owner state.
void ResetServiceDiagnosticHistory();

}  // namespace sco::diagnostics
