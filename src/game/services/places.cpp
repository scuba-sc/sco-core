// SPDX-License-Identifier: GPL-3.0-only
#include "sc_places.h"
#include "sco_api.h"
#include "sco/host.h"
#include "sco/runtime.h"

#include <cstring>
#include <vector>
#include <string>
#include <mutex>

namespace sco::game::services::places {
namespace {

std::mutex g_lock;
bool g_published = false;
std::vector<sc_place_info> g_places;

void InitDefaultPlacesLocked() {
    g_places.clear();
    const struct {
        uint64_t id;
        uint64_t parent;
        uint64_t zone;
        const char* name;
    } default_data[] = {
        { 1, 0, 0x1, "Stanton" },
        { 2, 1, 0x2, "Hurston" },
        { 3, 2, 0x4, "Lorville" },
        { 4, 1, 0x3, "Crusader" },
        { 5, 4, 0x5, "Orison" }
    };

    for (const auto& d : default_data) {
        sc_place_info p{};
        p.size = sizeof(p);
        p.flags = 0;
        p.place_id = d.id;
        p.parent_place_id = d.parent;
        p.zone_id = d.zone;
        std::strncpy(p.name, d.name, sizeof(p.name) - 1);
        g_places.push_back(p);
    }
}

sco_result Count(sco_plugin* self, uint32_t* out_count) {
    (void)self;
    if (!out_count) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    *out_count = static_cast<uint32_t>(g_places.size());
    return SCO_OK;
}

sco_result ByIndex(sco_plugin* self, uint32_t index, sc_place_info* out_place) {
    (void)self;
    if (!out_place || out_place->size < sizeof(sc_place_info)) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (index >= g_places.size()) return SCO_NOT_FOUND;
    *out_place = g_places[index];
    return SCO_OK;
}

sco_result ByName(sco_plugin* self, const char* name, sc_place_info* out_place) {
    (void)self;
    if (!name || !out_place || out_place->size < sizeof(sc_place_info)) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    for (const auto& p : g_places) {
        if (std::strcmp(p.name, name) == 0) {
            *out_place = p;
            return SCO_OK;
        }
    }
    return SCO_NOT_FOUND;
}

sco_result ById(sco_plugin* self, uint64_t place_id, sc_place_info* out_place) {
    (void)self;
    if (!out_place || out_place->size < sizeof(sc_place_info)) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    for (const auto& p : g_places) {
        if (p.place_id == place_id) {
            *out_place = p;
            return SCO_OK;
        }
    }
    return SCO_NOT_FOUND;
}

const sc_places_v1 kTable = {
    sizeof(sc_places_v1),
    0,
    Count,
    ByIndex,
    ByName,
    ById
};

} // namespace

sco_result Start() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_published) return SCO_FAILED;
    InitDefaultPlacesLocked();
    const sco::Result r = sco::host::ProvideGameService(
        SC_PLACES_SERVICE_NAME,
        SC_PLACES_SERVICE_VERSION,
        &kTable
    );
    if (r == sco::Result::Ok) {
        g_published = true;
        return SCO_OK;
    }
    return SCO_FAILED;
}

void Stop() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_published) return;
    sco::host::WithdrawGameService(SC_PLACES_SERVICE_NAME);
    g_published = false;
    g_places.clear();
}

} // namespace sco::game::services::places
