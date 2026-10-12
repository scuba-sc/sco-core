// GPL-3.0-only. Complete member extent and null guards, including failed reopen.
#ifndef SCOSDK_GAME_MISSIONS_HPP
#define SCOSDK_GAME_MISSIONS_HPP
#include "sc_missions.h"
namespace sco::sdk::game {
class Missions {
    const sc_missions_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr; self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* p = nullptr;
        const auto r = api->query_service(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION, &p);
        if (r != SCO_OK) return r;
        if (!p || *static_cast<const uint32_t*>(p) < 8) return SCO_UNAVAILABLE;
        table_ = static_cast<const sc_missions_v1*>(p);
        return SCO_OK;
    }
    sco_result Offer(uint32_t index, sc_mission_offer& out) const noexcept {
        return table_ && table_->size >= offsetof(sc_missions_v1, offer) + sizeof(table_->offer) && table_->offer
            ? table_->offer(self_, index, &out) : SCO_UNAVAILABLE;
    }
    sco_result Accept(const sc_mission_offer& offer, uint64_t key, uint64_t& id) const noexcept {
        return table_ && table_->size >= offsetof(sc_missions_v1, accept) + sizeof(table_->accept) && table_->accept
            ? table_->accept(self_, &offer, key, &id) : SCO_UNAVAILABLE;
    }
    sco_result Status(uint64_t id, sc_mission_status& out) const noexcept {
        return table_ && table_->size >= offsetof(sc_missions_v1, status) + sizeof(table_->status) && table_->status
            ? table_->status(self_, id, &out) : SCO_UNAVAILABLE;
    }
};
}
#endif
