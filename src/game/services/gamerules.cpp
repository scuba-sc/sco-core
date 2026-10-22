#include "gamerules.h"
#include "sco/game/signatures.h"
#include "sco/host.h"
#include "sco/caps.h"
#include "../../api/internal.h"
#include <algorithm>
#include <cstring>
#if defined(_WIN32) && defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace sco::game::services {
namespace {
GameRulesBackend g_backend;
bool g_bound = false;
bool g_faulted = false;
bool g_reading = false;
int ReadCpp(sc_gamerules_state* out) {
    try { return g_backend.read(g_backend.ctx, out) ? 1 : 0; }
    catch (...) { return -1; }
}
int ReadGuarded(sc_gamerules_state* out) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try { return ReadCpp(out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
#else
    return ReadCpp(out);
#endif
}
sc_gamerules_v1 g_table = {sizeof(sc_gamerules_v1), 0, nullptr, nullptr};
// Static parameter definitions: the 15 GameRules console variables whose names occur on the
// verified build 4.10.197.2878 as g_gameRules.<group>.<name> (F-4.10.197.2878-054), in fixed
// registry order. Names, groups and types only; no values, offsets or pointers, and nothing here
// reads game memory. The INT/FLOAT types are not verified on a captured build. Not run in game.
#define SC_GR_PARAM(type, group, name) {sizeof(sc_gamerules_param_info), type, name, group}
constexpr sc_gamerules_param_info kParams[] = {
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "control", "capturableDebug"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "control", "phaseDebug"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "control", "objectiveDebug"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "state", "log"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "scoring", "enabled"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "scoring", "log"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "damagehandling", "log"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "damagehandling", "disableCollisionFilter"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "", "showRaceCheckpoints"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "", "showRaceInfo"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_FLOAT, "", "overrideRespawnTimer"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "", "editorUseSpawnpoints"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "", "allowNonLocationSpawnpoints"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "", "despawnOnProjectileHit"),
    SC_GR_PARAM(SC_GAMERULES_PARAM_INT, "", "allowAllPlayerCameras"),
};
#undef SC_GR_PARAM
constexpr uint32_t kParamCount = static_cast<uint32_t>(sizeof(kParams) / sizeof(kParams[0]));
static_assert(kParamCount <= 0xFFFFu, "registry stays small");
sco_result Read(sco_plugin* self, sc_gamerules_state* out) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    if (detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    if (g_faulted || g_reading || !g_bound || !g_backend.read || !out || out->size < sizeof(sc_gamerules_state))
        return SCO_UNAVAILABLE;
    const uint32_t size = out->size;
    sc_gamerules_state value{};
    value.size = size;
    g_reading = true;
    const int result = ReadGuarded(&value);
    g_reading = false;
    if (result < 0) { g_faulted = true; return SCO_FAILED; }
    if (!result) return SCO_UNAVAILABLE;
    value.size = size;
    *out = value;
    return SCO_OK;
}
// Every check that can fail runs before the first write, so a failure leaves all outputs alone.
// Slot fields are touched through memcpy: stride need not keep the record aligned.
sco_result ListParams(sco_plugin* self, uint32_t offset, sc_gamerules_param_info* out,
                      uint32_t stride, uint32_t cap, uint32_t* out_count, uint32_t* out_total) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    if (!out_count || !out_total || out_count == out_total) return SCO_BAD_ARG;
    if (offset > kParamCount || cap > SC_GAMERULES_PARAM_MAX_PAGE) return SCO_BAD_ARG;
    if (cap == 0) {
        if (out) return SCO_BAD_ARG;
    } else if (!out || stride < sizeof(sc_gamerules_param_info) ||
               stride > SC_GAMERULES_PARAM_MAX_STRIDE) {
        return SCO_BAD_ARG;
    }
    const uint32_t n = std::min(cap, kParamCount - offset);
    auto* const base = reinterpret_cast<unsigned char*>(out);
    for (uint32_t i = 0; i != n; ++i) {
        uint32_t slot_size = 0;
        std::memcpy(&slot_size, base + static_cast<size_t>(i) * stride, sizeof(slot_size));
        if (slot_size < sizeof(sc_gamerules_param_info)) return SCO_BAD_ARG;
    }
    if (!g_table.list_params || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    for (uint32_t i = 0; i != n; ++i) {
        unsigned char* const slot = base + static_cast<size_t>(i) * stride;
        const sc_gamerules_param_info& def = kParams[offset + i];
        std::memcpy(slot + offsetof(sc_gamerules_param_info, type), &def.type, sizeof(def.type));
        std::memcpy(slot + offsetof(sc_gamerules_param_info, name), def.name, sizeof(def.name));
        std::memcpy(slot + offsetof(sc_gamerules_param_info, group), def.group, sizeof(def.group));
    }
    *out_count = n;
    *out_total = kParamCount;
    return SCO_OK;
}
}  // namespace
Result StartGameRules(const GameRulesBackend& backend) {
    if (!OnGameThread()) return Result::WrongThread;
    if (g_reading) return Result::Unavailable;
    if (g_table.read) return Result::Failed;
    g_bound = backend.read != nullptr;
    g_backend = backend;
    g_table.read = Read;
    g_table.list_params = ListParams;
    const Result r = host::ProvideGameService(SC_GAMERULES_SERVICE_NAME,
                                              SC_GAMERULES_VERSION_1_1, &g_table);
    if (r != Result::Ok) {
        g_table.read = nullptr; g_table.list_params = nullptr; g_backend = {}; g_bound = false;
    }
    if (r == Result::Ok) caps::Set("game.gamerules.params", true, "static definitions only; no live values");
    return r;
}
void StopGameRules() {
    if (!OnGameThread() || g_reading) return;
    if (!g_table.read) return;
    host::WithdrawGameService(SC_GAMERULES_SERVICE_NAME);
    caps::Set("game.gamerules.params", false, "gamerules stopped");
    g_table.read = nullptr;
    g_table.list_params = nullptr;
    g_backend = {};
    g_bound = false;
}
}  // namespace sco::game::services
