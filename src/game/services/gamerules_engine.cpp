// SPDX-License-Identifier: GPL-3.0-only
#include "gamerules_engine.h"
namespace sco::game::services {
namespace {
GameRulesNativeRoute g_route;
GameRulesBackend g_backend;
const char* g_refusal = "";
// The last copied state and the generation it came from. A read whose generation is unchanged
// reuses this copy: the blob is not re-read, so a value cannot change between two reads that the
// host counted as one change.
struct Cache {
    uint64_t generation = 0;
    sc_gamerules_state state{};
    bool valid = false;
};
Cache g_cache;

bool NativeRead(void*, sc_gamerules_state* out) {
    if (!out || !g_route.generation || !g_route.read_only_accessor) return false;
    const uint64_t generation = g_route.generation(g_route.ctx);
    // Zero is the host saying it has no valid count; refuse rather than guess freshness.
    if (!generation) return false;
    if (g_cache.valid && g_cache.generation == generation) {
        *out = g_cache.state;
        return true;
    }
    sc_gamerules_state value{};
    value.size = sizeof(sc_gamerules_state);
    if (!g_route.read_only_accessor(g_route.ctx, &value)) return false;
    // The accessor fills named fields only; the size is ours, so a route cannot report a layout
    // this build does not have and no field is shifted.
    value.size = sizeof(sc_gamerules_state);
    g_cache.generation = generation;
    g_cache.state = value;
    g_cache.valid = true;
    *out = value;
    return true;
}
}  // namespace

const char* NativeGameRulesRefusal() { return g_refusal; }

Result InstallNativeGameRules(const GameRulesNativeRoute& route) {
    g_cache = {};
    if (g_backend.read) {
        g_refusal = "a native GameRules backend is already installed";
        return Result::Failed;
    }
    if (!route.params_module) {
        g_refusal = "the IGameRulesParamsModule instance was not resolved";
        return Result::Unavailable;
    }
    if (!route.read_only_accessor) {
        g_refusal = "no read-only accessor for the params blob: the only grounded route applies state";
        return Result::Unavailable;
    }
    if (!route.generation) {
        g_refusal = "no host-owned change counter for the params blob";
        return Result::Unavailable;
    }
    g_route = route;
    g_backend.ctx = nullptr;
    g_backend.read = NativeRead;
    g_refusal = "";
    return Result::Ok;
}

GameRulesBackend NativeGameRulesBackend() { return g_backend; }

}  // namespace sco::game::services
