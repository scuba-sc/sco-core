#include "gamerules.h"
#include "sco/game/signatures.h"
#include "sco/host.h"
namespace sco::game::services {
namespace {
GameRulesBackend g_backend;
bool g_bound = false;
sc_gamerules_v1 g_table = {sizeof(sc_gamerules_v1), 0, nullptr};
sco_result Read(sco_plugin* self, sc_gamerules_state* out) {
    (void)self;
    if (!g_bound || !g_backend.read || !out || out->size < sizeof(sc_gamerules_state))
        return SCO_UNAVAILABLE;
    const uint32_t size = out->size;
    if (!g_backend.read(g_backend.ctx, out)) return SCO_UNAVAILABLE;
    out->size = size;
    return SCO_OK;
}
}  // namespace
Result StartGameRules(const GameRulesBackend& backend) {
    if (g_table.read) return Result::Failed;
    g_bound = backend.read != nullptr;
    g_backend = backend;
    g_table.read = Read;
    const Result r = host::ProvideGameService(SC_GAMERULES_SERVICE_NAME,
                                              SC_GAMERULES_SERVICE_VERSION, &g_table);
    if (r != Result::Ok) { g_table.read = nullptr; g_backend = {}; g_bound = false; }
    return r;
}
void StopGameRules() {
    if (!g_table.read) return;
    host::WithdrawGameService(SC_GAMERULES_SERVICE_NAME);
    g_table.read = nullptr;
    g_backend = {};
    g_bound = false;
}
}  // namespace sco::game::services
