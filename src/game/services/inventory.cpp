#include "inventory.h"
#include "sco/host.h"
#include <cstring>
namespace sco::game::services {
namespace {
InventoryBackend g_backend;
bool g_bound = false;
sc_inventory_v1_2 g_table = {{sizeof(sc_inventory_v1_2), 0, nullptr}, inventory_layout::Snapshot};
sco_result Snapshot(sco_plugin* self, sc_inventory_snapshot* out) {
    (void)self;
    if (!g_bound) return SCO_UNAVAILABLE;
    return SnapshotWith(g_backend, out);
}
}  // namespace

sco_result SnapshotWith(const InventoryBackend& backend, sc_inventory_snapshot* out) {
    if (!backend.snapshot || !out || out->size < SC_INVENTORY_SNAPSHOT_SIZE_1_0) return SCO_UNAVAILABLE;
    const uint32_t size = out->size;
    const uint32_t capacity = out->count < SC_INVENTORY_MAX_PORTS ? out->count : SC_INVENTORY_MAX_PORTS;
    sc_inventory_snapshot full;
    memset(&full, 0, sizeof(full));
    full.size = sizeof(full);
    full.count = capacity;
    if (!backend.snapshot(backend.ctx, &full)) return SCO_UNAVAILABLE;
    const uint32_t count = full.count < capacity ? full.count : capacity;
    uint32_t total = full.total_seen < count ? count : full.total_seen;
    uint32_t flags = full.flags & (SC_INVENTORY_FLAG_TRUNCATED | SC_INVENTORY_FLAG_LIMITED);
    if (total > count) flags |= SC_INVENTORY_FLAG_TRUNCATED;
    else flags &= ~SC_INVENTORY_FLAG_TRUNCATED;
    out->count = count;
    out->generation = full.generation;
    memcpy(out->ports, full.ports, count * sizeof(sc_inventory_port));
    if (size >= sizeof(sc_inventory_snapshot)) {
        out->flags = flags;
        out->total_seen = total;
    }
    out->size = size;
    return SCO_OK;
}
Result StartGameInventory(const InventoryBackend& backend) {
    return StartGameInventoryWithLayout(backend, {});
}
Result StartGameInventoryWithLayout(const InventoryBackend& backend, const InventoryLayoutBackend& layout) {
    if (g_table.base.snapshot) return Result::Failed;
    const Result prepared = inventory_layout::Begin(layout);
    if (prepared != Result::Ok) return prepared;
    g_bound = backend.snapshot != nullptr;
    g_backend = backend;
    g_table.base.snapshot = Snapshot;
    const Result r = host::ProvideGameService(SC_INVENTORY_SERVICE_NAME,
                                              SC_INVENTORY_SERVICE_VERSION_1_2, &g_table);
    if (r != Result::Ok) { g_table.base.snapshot = nullptr; g_backend = {}; g_bound = false; inventory_layout::Stop(); }
    return r;
}
void StopGameInventory() {
    if (!g_table.base.snapshot) return;
    host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME);
    g_table.base.snapshot = nullptr;
    g_backend = {};
    g_bound = false;
    inventory_layout::Stop();
}
}  // namespace sco::game::services
