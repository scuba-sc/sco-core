// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/runtime.h"
#include "game/services/inventory.h"
#include "scosdk/game/inventory_layout.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>

namespace S = sco::game::services;
unsigned checks;
void Check(bool value) { ++checks; if (!value) { std::fprintf(stderr, "inventory layout failure %u\n", checks); std::exit(1); } }
struct World {
    bool admitted = true, oversizedInfo = false, duplicateKeys = false;
    bool unknown = false, empty = false, reorder = false, reenter = false, releaseDuringRead = false, restartDuringRead = false;
    uint64_t epoch = 1, generation = 1, entity = 100;
    unsigned reads = 0;
    sco_plugin* owner = nullptr;
    const sc_inventory_v1_2* table = nullptr;
    sco_result nested = SCO_OK;
} world;
bool Admitted(void* context) { return static_cast<World*>(context)->admitted; }
sco_result Read(void* context, sc_inventory_layout_snapshot* out, sc_inventory_layout_entry* rows, uint32_t capacity) {
    auto& w = *static_cast<World*>(context); ++w.reads; Check(capacity == 128);
    if (w.reenter) {
        sc_inventory_layout_snapshot nested{}; nested.size = sizeof(nested);
        sc_inventory_layout_entry entry{}; entry.size = sizeof(entry);
        w.nested = w.table->snapshot_layout(w.owner, &nested, &entry, sizeof(entry), 1);
    }
    if (w.releaseDuringRead) Check(sco::Release(w.owner) == sco::Result::Ok);
    if (w.restartDuringRead) {
        S::StopGameInventory();
        Check(S::StartGameInventoryWithLayout({}, {&w, true, Admitted, Read}) == sco::Result::Ok);
    }
    out->count = out->total_seen = 3; out->flags = SC_INVENTORY_LAYOUT_COMPLETE;
    out->source_epoch = w.epoch; out->observation_generation = w.generation;
    if (w.oversizedInfo) out->size = sizeof(*out) + 8;
    std::strcpy(out->identity_domain, "dev.gear.v1"); std::strcpy(out->layout_key, "human.v1");
    for (unsigned i = 0; i < 3; ++i) {
        rows[i].role = i == 1 ? SC_INVENTORY_ROLE_STORAGE : SC_INVENTORY_ROLE_EQUIPMENT;
        rows[i].state = i == 2 ? SC_INVENTORY_PORT_EMPTY : SC_INVENTORY_PORT_OCCUPIED;
        if (i != 2) { rows[i].flags = SC_INVENTORY_ENTRY_TYPE_KNOWN | SC_INVENTORY_ENTRY_ENTITY_KNOWN; rows[i].raw_item_type = i ? UINT32_MAX : 22; rows[i].entity_id = w.entity + i; }
        std::strcpy(rows[i].port_key, i == 0 ? "helmet" : i == 1 ? "storage" : "holster");
    }
    if (w.duplicateKeys) std::strcpy(rows[2].port_key, rows[0].port_key);
    if (w.unknown) {
        rows[0].state = SC_INVENTORY_PORT_UNKNOWN; rows[0].role = SC_INVENTORY_ROLE_UNKNOWN;
        rows[0].flags = rows[0].raw_item_type = 0; rows[0].entity_id = 0;
    }
    if (w.reorder) std::swap(rows[0], rows[2]);
    if (w.empty) out->count = out->total_seen = 0;
    return SCO_OK;
}
int main() {
    sco::SetGameThread(); const auto* api = sco::host::BuildApi({"DEV inventory layout fixture"});
    auto* self = sco::host::NewPlugin("layout_test"); Check(self != nullptr);
    Check(S::StartGameInventory() == sco::Result::Ok);
    const void* defaultRaw = nullptr;
    Check(api->query_service(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_2, &defaultRaw) == SCO_OK && defaultRaw);
    Check(!api->has(SC_INVENTORY_LAYOUT_CAPABILITY));
    sc_inventory_layout_snapshot unavailable{}; unavailable.size = sizeof(unavailable);
    sc_inventory_layout_entry unavailableEntry{}; unavailableEntry.size = sizeof(unavailableEntry);
    Check(static_cast<const sc_inventory_v1_2*>(defaultRaw)->snapshot_layout(self, &unavailable, &unavailableEntry,
          sizeof(unavailableEntry), 1) == SCO_UNAVAILABLE);
    S::StopGameInventory();
    const S::InventoryLayoutBackend backend{&world, true, Admitted, Read};
    Check(S::StartGameInventoryWithLayout({}, backend) == sco::Result::Ok);
    const void* raw = nullptr; Check(api->query_service(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_2, &raw) == SCO_OK && raw);
    const auto* table = static_cast<const sc_inventory_v1_2*>(raw); Check(table->base.size == 24);
    world.owner = self; world.table = table;
    sc_inventory_layout_snapshot out{}; out.size = sizeof(out);
    sc_inventory_layout_entry rows[3]{}; for (auto& row : rows) row.size = sizeof(row);
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK);
    Check(out.count == 3 && out.total_seen == 3 && out.flags == SC_INVENTORY_LAYOUT_COMPLETE);
    Check(sc_inventory_layout_valid(&out, rows, sizeof(rows[0]), 3));
    Check(rows[1].raw_item_type == UINT32_MAX && rows[1].role == SC_INVENTORY_ROLE_STORAGE);
    Check(rows[2].state == SC_INVENTORY_PORT_EMPTY && rows[2].flags == 0);
    Check(api->has(SC_INVENTORY_LAYOUT_CAPABILITY));
    sco::sdk::game::InventoryLayout sdk;
    Check(sdk.Open(api, self) == SCO_OK);
    sc_inventory_layout_snapshot sdkInfo{};
    sc_inventory_layout_entry sdkRows[3]{};
    Check(sdk.Snapshot(sdkInfo, sdkRows) == SCO_OK && sdkInfo.count == 3 &&
          std::strcmp(sdkRows[0].port_key, "helmet") == 0);
    const auto sdkSaved = sdkInfo;
    world.admitted = false;
    Check(sdk.Snapshot(sdkInfo, sdkRows) == SCO_UNAVAILABLE && !std::memcmp(&sdkInfo, &sdkSaved, sizeof(sdkInfo)));
    world.admitted = true;
    const void* old = nullptr;
    Check(api->query_service(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_0, &old) == SCO_OK && old);
    Check(static_cast<const sc_inventory_v1*>(old)->snapshot != nullptr);
    old = nullptr;
    Check(api->query_service(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_1, &old) == SCO_OK && old);

    const auto saved = out;
    sc_inventory_layout_entry savedRows[3]; std::memcpy(savedRows, rows, sizeof(rows));
    const unsigned before = world.reads;
    Check(table->snapshot_layout(self, &out, nullptr, sizeof(rows[0]), 3) == SCO_BAD_ARG);
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 0) == SCO_BAD_ARG);
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 129) == SCO_TOO_MANY);
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]) - 8, 3) == SCO_BAD_ARG);
    Check(world.reads == before && !std::memcmp(&out, &saved, sizeof(out)) && !std::memcmp(rows, savedRows, sizeof(rows)));
    alignas(8) unsigned char odd[sizeof(out) + 8]{};
    Check(table->snapshot_layout(self, reinterpret_cast<sc_inventory_layout_snapshot*>(odd + 1), rows,
                                  sizeof(rows[0]), 3) == SCO_BAD_ARG);

    world.oversizedInfo = true; ++world.generation;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_UNAVAILABLE);
    Check(!std::memcmp(&out, &saved, sizeof(out)) && !std::memcmp(rows, savedRows, sizeof(rows)));
    world.oversizedInfo = false; world.duplicateKeys = true; ++world.generation;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_UNAVAILABLE);
    Check(!std::memcmp(&out, &saved, sizeof(out)) && !std::memcmp(rows, savedRows, sizeof(rows)));
    world.duplicateKeys = false; ++world.generation;

    struct WideInfo { sc_inventory_layout_snapshot value; uint64_t tail; } wideInfo{};
    struct WideRow { sc_inventory_layout_entry value; uint64_t tail; } wideRows[2]{};
    wideInfo.value.size = sizeof(wideInfo); wideInfo.tail = UINT64_C(0x1122334455667788);
    for (auto& row : wideRows) { row.value.size = sizeof(row); row.tail = UINT64_C(0xaabbccddeeff0011); }
    Check(table->snapshot_layout(self, &wideInfo.value, &wideRows[0].value, sizeof(WideRow), 2) == SCO_OK);
    Check(wideInfo.value.count == 2 && wideInfo.value.total_seen == 3 &&
          wideInfo.value.flags == SC_INVENTORY_LAYOUT_TRUNCATED);
    Check(wideInfo.value.size == sizeof(wideInfo) && wideInfo.tail == UINT64_C(0x1122334455667788));
    for (const auto& row : wideRows) Check(row.value.size == sizeof(WideRow) && row.tail == UINT64_C(0xaabbccddeeff0011));
    const auto firstEpoch = wideInfo.value.source_epoch;
    world.admitted = false;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_UNAVAILABLE);
    world.admitted = true;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK && out.source_epoch != firstEpoch);
    const auto reorderedEpoch = out.source_epoch;
    world.reorder = true; // Order alone does not change a coherent semantic frame.
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK && out.source_epoch == reorderedEpoch);
    Check(std::strcmp(rows[0].port_key, "holster") == 0);
    world.reorder = false; world.empty = true; ++world.generation;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK && out.count == 0 &&
          out.total_seen == 0 && out.flags == SC_INVENTORY_LAYOUT_COMPLETE);
    world.empty = false; ++world.generation;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK && out.count == 3);

    sco_result offThread = SCO_OK;
    const unsigned beforeThread = world.reads;
    std::thread thread([&] { offThread = table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3); });
    thread.join();
    Check(offThread == SCO_WRONG_THREAD && world.reads == beforeThread);
    world.unknown = true; ++world.generation;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK);
    Check(rows[0].state == SC_INVENTORY_PORT_UNKNOWN && rows[0].role == SC_INVENTORY_ROLE_UNKNOWN && rows[0].flags == 0);
    world.unknown = false; world.reenter = true; ++world.generation;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK && world.nested == SCO_UNAVAILABLE);
    world.reenter = false;
    world.restartDuringRead = true; ++world.generation;
    const auto beforeRestart = out;
    std::memcpy(savedRows, rows, sizeof(rows));
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_UNAVAILABLE);
    Check(!std::memcmp(&out, &beforeRestart, sizeof(out)) && !std::memcmp(rows, savedRows, sizeof(rows)));
    world.restartDuringRead = false;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK);
    ++world.entity; // A changed observation cannot reuse the same provider revision.
    const auto beforeIncoherent = out;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_UNAVAILABLE);
    Check(!std::memcmp(&out, &beforeIncoherent, sizeof(out)));
    ++world.generation;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_OK);
    world.releaseDuringRead = true; ++world.generation;
    const auto beforeRelease = out;
    std::memcpy(savedRows, rows, sizeof(rows));
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_BAD_ARG);
    Check(!std::memcmp(&out, &beforeRelease, sizeof(out)) && !std::memcmp(rows, savedRows, sizeof(rows)));
    world.releaseDuringRead = false;
    Check(table->snapshot_layout(self, &out, rows, sizeof(rows[0]), 3) == SCO_BAD_ARG);
    auto* fresh = sco::host::NewPlugin("layout_fresh"); Check(fresh != nullptr);
    world.owner = fresh;
    Check(table->snapshot_layout(fresh, &out, rows, sizeof(rows[0]), 3) == SCO_OK);
    S::StopGameInventory(); std::printf("inventory layout provider: %u checks, 0 failed\n", checks);
}
