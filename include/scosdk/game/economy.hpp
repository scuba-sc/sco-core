// GPL-3.0-only. Guarded direct C table adapter; no import/save/payment policy.
#ifndef SCOSDK_GAME_ECONOMY_HPP
#define SCOSDK_GAME_ECONOMY_HPP
#include "sc_economy.h"
#include <cstddef>
namespace sco::sdk::game {
class Economy {
    const sc_economy_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr; self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* p = nullptr;
        const auto r = api->query_service(SC_ECONOMY_SERVICE_NAME, SC_ECONOMY_SERVICE_VERSION, &p);
        if (r != SCO_OK) return r;
        if (!p || *static_cast<const uint32_t*>(p) < 8) return SCO_UNAVAILABLE;
        table_ = static_cast<const sc_economy_v1*>(p);
        return SCO_OK;
    }
    sco_result Balance(sc_economy_balance& out) const noexcept {
        return table_ && table_->size >= offsetof(sc_economy_v1, balance) + sizeof(table_->balance) && table_->balance
            ? table_->balance(self_, &out) : SCO_UNAVAILABLE;
    }
    sco_result Apply(const sc_economy_request& request, sc_economy_receipt& out) const noexcept {
        return table_ && table_->size >= offsetof(sc_economy_v1, apply) + sizeof(table_->apply) && table_->apply
            ? table_->apply(self_, &request, &out) : SCO_UNAVAILABLE;
    }
    sco_result Receipt(uint64_t key, sc_economy_receipt& out) const noexcept {
        return table_ && table_->size >= offsetof(sc_economy_v1, receipt) + sizeof(table_->receipt) && table_->receipt
            ? table_->receipt(self_, key, &out) : SCO_UNAVAILABLE;
    }
};
}
#endif
