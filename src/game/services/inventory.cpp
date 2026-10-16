#include "inventory.h"
#include "sco/host.h"
#include <cstring>
namespace sco::game::services {
namespace {
InventoryBackend g_backend;
bool g_bound = false;
sc_inventory_v1 g_table = {sizeof(sc_inventory_v1), 0, nullptr};
sco_result Snapshot(sco_plugin* self, sc_inventory_snapshot* out) {
    (void)self;
    if (!g_bound || !g_backend.snapshot || !out || out->size < sizeof(sc_inventory_snapshot))
        return SCO_UNAVAILABLE;
    const uint32_t size = out->size;
    if (!g_backend.snapshot(g_backend.ctx, out)) return SCO_UNAVAILABLE;
    out->size = size;
    return SCO_OK;
}
}  // namespace
Result StartGameInventory(const InventoryBackend& backend) {
    if (g_table.snapshot) return Result::Failed;
    g_bound = backend.snapshot != nullptr;
    g_backend = backend;
    g_table.snapshot = Snapshot;
    const Result r = host::ProvideGameService(SC_INVENTORY_SERVICE_NAME,
                                              SC_INVENTORY_SERVICE_VERSION, &g_table);
    if (r != Result::Ok) { g_table.snapshot = nullptr; g_backend = {}; g_bound = false; }
    return r;
}
void StopGameInventory() {
    if (!g_table.snapshot) return;
    host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME);
    g_table.snapshot = nullptr;
    g_backend = {};
    g_bound = false;
}
}  // namespace sco::game::services
