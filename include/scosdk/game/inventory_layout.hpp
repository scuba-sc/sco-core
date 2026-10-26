// SPDX-License-Identifier: GPL-3.0-only
#ifndef SCOSDK_GAME_INVENTORY_LAYOUT_HPP
#define SCOSDK_GAME_INVENTORY_LAYOUT_HPP

#include "../plugin.hpp"
#include "sc_inventory.h"
#include <array>
#include <span>

namespace sco::sdk::game {
// Copied, read-only observations; an available table does not establish a producer.
class InventoryLayout {
public:
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        api_ = nullptr; self_ = nullptr; table_ = nullptr;
        if (!api || !self) return SCO_BAD_ARG;
        if (!Covers(api->size, offsetof(sco_api, query_service)) || !api->query_service) return SCO_UNAVAILABLE;
        const void* raw = nullptr;
        const auto result = api->query_service(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_2, &raw);
        if (result != SCO_OK) return result;
        const auto* table = static_cast<const sc_inventory_v1_2*>(raw);
        if (!table || !Covers(table->base.size, offsetof(sc_inventory_v1_2, snapshot_layout)) || !table->snapshot_layout)
            return SCO_UNAVAILABLE;
        api_ = api; self_ = self; table_ = table;
        return SCO_OK;
    }

    // Success replaces info and the first info.count rows; failure changes neither.
    sco_result Snapshot(sc_inventory_layout_snapshot& info, std::span<sc_inventory_layout_entry> rows) const noexcept {
        if (rows.empty() || rows.size() > SC_INVENTORY_LAYOUT_MAX_PORTS) return SCO_BAD_ARG;
        const auto infoStart = reinterpret_cast<uintptr_t>(&info);
        const auto rowStart = reinterpret_cast<uintptr_t>(rows.data());
        const auto rowBytes = rows.size() * sizeof(rows[0]);
        if (infoStart > UINTPTR_MAX - sizeof(info) || rowStart > UINTPTR_MAX - rowBytes ||
            (infoStart < rowStart + rowBytes && rowStart < infoStart + sizeof(info))) return SCO_BAD_ARG;
        if (!api_ || !Covers(api_->size, offsetof(sco_api, has)) || !api_->has ||
            !api_->has(SC_INVENTORY_LAYOUT_CAPABILITY) || !table_ ||
            !Covers(table_->base.size, offsetof(sc_inventory_v1_2, snapshot_layout)) || !table_->snapshot_layout)
            return SCO_UNAVAILABLE;
        sc_inventory_layout_snapshot copied{}; copied.size = sizeof(copied);
        std::array<sc_inventory_layout_entry, SC_INVENTORY_LAYOUT_MAX_PORTS> entries{};
        for (auto& entry : entries) entry.size = sizeof(entry);
        const auto capacity = static_cast<uint32_t>(rows.size());
        const auto result = table_->snapshot_layout(self_, &copied, entries.data(), sizeof(entries[0]), capacity);
        if (result != SCO_OK) return result;
        if (copied.size != sizeof(copied) || !sc_inventory_layout_valid(&copied, entries.data(), sizeof(entries[0]), capacity))
            return SCO_FAILED;
        for (uint32_t i = 0; i < copied.count; ++i) rows[i] = entries[i];
        info = copied;
        return SCO_OK;
    }
private:
    const sco_api* api_ = nullptr;
    sco_plugin* self_ = nullptr;
    const sc_inventory_v1_2* table_ = nullptr;
};
}
#endif
