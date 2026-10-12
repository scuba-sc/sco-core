// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sc_cvars.h>
#include <cstddef>
namespace scosdk::game {
class Cvars {
    const sco_api* api_;
    sco_plugin* self_;
    const sc_cvars_v1* table_ = nullptr;
public:
    Cvars(const sco_api* api, sco_plugin* self) : api_(api), self_(self) {}
    sco_result Open() noexcept {
        table_ = nullptr;
        if (!api_ || api_->size < offsetof(sco_api, query_service) + sizeof(api_->query_service) ||
            !api_->query_service) return SCO_UNAVAILABLE;
        const void* raw = nullptr;
        const sco_result r = api_->query_service(SC_CVARS_NAME, SC_CVARS_VERSION, &raw);
        if (r != SCO_OK) return r;
        const auto* t = static_cast<const sc_cvars_v1*>(raw);
        if (!t || t->size < offsetof(sc_cvars_v1, get_bool) + sizeof(t->get_bool) || !t->get_bool)
            return SCO_UNAVAILABLE;
        table_ = t;
        return SCO_OK;
    }
    sco_result GetBool(const char* name, uint32_t& out) const noexcept {
        return table_ ? table_->get_bool(self_, name, SC_CVAR_BOOL, &out) : SCO_UNAVAILABLE;
    }
};
}
