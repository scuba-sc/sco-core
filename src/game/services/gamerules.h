#pragma once
#include "sco/runtime.h"
#include <sc_gamerules.h>
namespace sco::game::services {
// game.gamerules 1.1: read-only server GameRules parameters (docs/game/gamerules.md).
// 1.1 adds list_params, a static registry of parameter definitions (name, group, type) that
// needs neither a backend nor the read capability and never reports a value; it is published by
// every StartGameRules call, bound or not. read behaves exactly as in 1.0.
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
