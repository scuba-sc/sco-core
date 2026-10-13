// GPL-3.0-only. game.prefabs C++ route, complete pointer guards.
#ifndef SCOSDK_GAME_PREFABS_HPP
#define SCOSDK_GAME_PREFABS_HPP
#include "../plugin.hpp"
#include "sc_prefabs.h"
namespace sco::sdk::game {
class Prefabs {
public:
    sco_result Open(const Plugin& p) noexcept { return Open(p.Api(), p.Self()); }
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr; self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (!Covers(api->size, offsetof(sco_api, query_service)) || !api->query_service) return SCO_UNAVAILABLE;
        const void* raw = nullptr;
        const auto r = api->query_service(SC_PREFABS_NAME, SC_PREFABS_VERSION_1_0, &raw);
        if (r != SCO_OK) return r;
        if (!raw || *static_cast<const uint32_t*>(raw) < 8) return SCO_UNAVAILABLE;
        table_ = static_cast<const sc_prefabs_v1*>(raw); return SCO_OK;
    }
    sco_result Spawn(const char* path, uint64_t zone, const double (&pos)[3], const double (&rot)[4], uint64_t& id) const noexcept {
        id = 0;
        if (!table_ || !Covers(table_->size, offsetof(sc_prefabs_v1, spawn)) || !table_->spawn) return SCO_UNAVAILABLE;
        return table_->spawn(self_, path, zone, pos, rot, &id);
    }
private:
    const sc_prefabs_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
};
}
#endif
