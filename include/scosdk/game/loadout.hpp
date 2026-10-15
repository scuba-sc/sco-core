// SPDX-License-Identifier: GPL-3.0-only
#ifndef SCOSDK_GAME_LOADOUT_HPP
#define SCOSDK_GAME_LOADOUT_HPP
#include "sc_loadout.h"
#include <span>
#include <cstddef>
namespace sco::sdk::game {
// Borrowed session-lifetime game table. Reopen after host session changes.
class Loadout {
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        return Open(api, self, SC_LOADOUT_SERVICE_VERSION);
    }
    // Require at least minVersion (e.g. SC_LOADOUT_VERSION_1_1 for weapon slots 8-21).
    sco_result Open(const sco_api* api, sco_plugin* self, uint32_t minVersion) noexcept {
        table_ = nullptr; self_ = nullptr;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* out = nullptr;
        const sco_result r = api->query_service(SC_LOADOUT_SERVICE_NAME, minVersion, &out);
        if (r == SCO_OK && out) { table_ = static_cast<const sc_loadout_v1*>(out); self_ = self; }
        return r == SCO_OK && !out ? SCO_UNAVAILABLE : r;
    }
    sco_result Apply(std::span<const sc_loadout_item> items, sc_loadout_report& report) const noexcept {
        report = {}; report.size = sizeof(report);
        if (!table_ || table_->size < offsetof(sc_loadout_v1, apply) + sizeof(table_->apply) || !table_->apply)
            return SCO_UNAVAILABLE;
        if (items.size() > SC_LOADOUT_MAX_ITEMS) return SCO_TOO_MANY;
        const sc_loadout_request request{sizeof(request), static_cast<uint32_t>(items.size()), sizeof(sc_loadout_item), 0, items.data()};
        return table_->apply(self_, &request, &report);
    }
private:
    const sc_loadout_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
};
}
#endif
