// SPDX-License-Identifier: GPL-3.0-only
#include "container_contents.h"
#include "sco/caps.h"
#include "sco/host.h"
#include "../../api/internal.h"
#include <cstring>
#include <limits>
namespace sco::game::services {
namespace {
ContainerContentsBackend backend;
bool started = false;
bool busy = false;
constexpr const char* capability = "game.inventory.contents.read";
sco_result Snapshot(sco_plugin* self, uint64_t id, sc_container_snapshot* out) {
    const bool sized = out && out->size >= sizeof(*out);
    const uint32_t size = sized ? out->size : 0;
    const uint32_t capacity = sized ? out->count : 0;
    if (sized) { std::memset(out, 0, sizeof(*out)); out->size = size; }
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self) || !id || !sized ||
        capacity > SC_CONTAINER_CONTENTS_MAX_ITEMS) return SCO_BAD_ARG;
    if (!started || !backend.snapshot || !caps::Has(capability) || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    if (busy) return SCO_FAILED;
    sc_container_snapshot full{};
    full.size = sizeof(full);
    full.count = SC_CONTAINER_CONTENTS_MAX_ITEMS;
    busy = true;
    sco_result result = SCO_FAILED;
    try { result = backend.snapshot(backend.ctx, id, &full); } catch (...) {}
    busy = false;
    if (detail::Released(self) || !caps::Has(capability) || detail::RuntimeStopping()) return SCO_UNAVAILABLE;
    if (result != SCO_OK) return result == SCO_NOT_FOUND || result == SCO_UNAVAILABLE ? result : SCO_FAILED;
    if (full.size != sizeof(full) || full.count > SC_CONTAINER_CONTENTS_MAX_ITEMS ||
        full.container_id != id || !full.generation || full.total_seen < full.count ||
        (full.flags & ~(SC_CONTAINER_CONTENTS_TRUNCATED | SC_CONTAINER_CONTENTS_VOLUME_KNOWN)) ||
        (full.total_seen > full.count && !(full.flags & SC_CONTAINER_CONTENTS_TRUNCATED))) return SCO_FAILED;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < full.count; ++i) {
        const auto& item = full.items[i];
        if (item.size != sizeof(item) || !item.item_id || !item.class_id || !item.quantity) return SCO_FAILED;
        for (uint32_t j = 0; j < i; ++j) if (full.items[j].item_id == item.item_id) return SCO_FAILED;
        if (full.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN) {
            if (item.unit_volume > std::numeric_limits<uint64_t>::max() / item.quantity) return SCO_FAILED;
            const uint64_t volume = item.unit_volume * item.quantity;
            if (sum > std::numeric_limits<uint64_t>::max() - volume) return SCO_FAILED;
            sum += volume;
        } else if (item.unit_volume) return SCO_FAILED;
    }
    if (!(full.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN) && full.used_volume) return SCO_FAILED;
    if ((full.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN) &&
        (sum > full.used_volume || (full.total_seen == full.count && sum != full.used_volume))) return SCO_FAILED;
    const uint32_t count = full.count < capacity ? full.count : capacity;
    out->count = count; out->generation = full.generation; out->container_id = id;
    out->used_volume = full.used_volume; out->total_seen = full.total_seen;
    out->flags = full.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN;
    if (full.total_seen > count) out->flags |= SC_CONTAINER_CONTENTS_TRUNCATED;
    std::memcpy(out->items, full.items, count * sizeof(full.items[0]));
    return SCO_OK;
}
const sc_container_contents_v1 table{sizeof(table), 0, Snapshot};
}
Result StartContainerContents(const ContainerContentsBackend& next) {
    if (!OnGameThread()) return Result::WrongThread;
    if (started || busy) return Result::Failed;
    const auto result = host::ProvideGameService(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &table);
    if (result != Result::Ok) return result;
    backend = next; started = true;
    caps::Set(capability, backend.snapshot != nullptr, "container contents require a host-owned read provider");
    return Result::Ok;
}
void StopContainerContents() {
    if (!OnGameThread() || !started || busy) return;
    host::WithdrawGameService(SC_CONTAINER_CONTENTS_NAME);
    caps::Set(capability, false, "container contents service stopped");
    backend = {}; started = false;
}
}
