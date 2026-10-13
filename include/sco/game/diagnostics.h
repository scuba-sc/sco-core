#pragma once
// GPL-3.0-only. Game-pack-owned metadata publisher, no engine/SDK callbacks.
#include "sco/diagnostics.h"
namespace sco::game::diagnostics {
// Called on host startup/tick boundaries; no scanner or provider execution.
// Version is this compiled pack, never its historically verified game build.
// Runtime gameBuild comes only from trusted host observation, otherwise unknown.
std::optional<sco::diagnostics::GamePackMetadata> CopyMinimal(
    const sco::diagnostics::SourceOptions& options, uint64_t nowMs);
}
