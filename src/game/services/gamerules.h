#pragma once
#include "sco/runtime.h"
#include <sc_gamerules.h>
namespace sco::game::services {
// game.gamerules 1.0: read-only server GameRules parameters (docs/game/gamerules.md).
// A host supplies the params accessor only after grounding the build's parameter layout
// against the gamerules.params_sync row. The default production configuration publishes
// nothing: capability game.gamerules.read is OFF, so an unbound backend cannot read memory.
struct GameRulesBackend {
    void* ctx = nullptr;
    bool (*read)(void* ctx, sc_gamerules_state* out);   // fills every field except size
};
Result StartGameRules(const GameRulesBackend& backend = {});
void StopGameRules();
}  // namespace sco::game::services
