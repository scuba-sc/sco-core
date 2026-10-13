// GPL-3.0-only. Minimal SC metadata only; never game memory or signature execution.
#include "sco/game/diagnostics.h"
#include "sc_game_pack.h"
namespace sco::game::diagnostics {
std::optional<sco::diagnostics::GamePackMetadata> CopyMinimal(
    const sco::diagnostics::SourceOptions& options, uint64_t nowMs) {
    if (!sco::OnGameThread() || !options.gamePack || !options.offlineAuthorized ||
        nowMs > sco::diagnostics::kMaxMonotonicMs) return {};
    sco::diagnostics::GamePackMetadata result{SCO_GAME_PACK_VERSION, options.gameBuild};
    result.observedMs = nowMs;
    return result;
}
}
