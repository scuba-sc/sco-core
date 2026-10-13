// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sc_cvars_write.h>
#include <cstddef>
namespace scosdk::game {
class CvarsWrite {
    const sco_api* api_;
    sco_plugin* self_;
    const sc_cvars_write_v1* table_ = nullptr;
public:
    CvarsWrite(const sco_api* api, sco_plugin* self) : api_(api), self_(self) {}
    sco_result Open() noexcept {
        table_ = nullptr;
        if (!api_ || api_->size < offsetof(sco_api, query_service) + sizeof(api_->query_service) ||
            !api_->query_service) return SCO_UNAVAILABLE;
        const void* raw = nullptr;
        const sco_result r = api_->query_service(SC_CVARS_WRITE_NAME, SC_CVARS_WRITE_VERSION, &raw);
        if (r != SCO_OK) return r;
        const auto* t = static_cast<const sc_cvars_write_v1*>(raw);
        if (!t || t->size < offsetof(sc_cvars_write_v1, restore_all) + sizeof(t->restore_all) ||
            !t->set_bool || !t->restore_all) return SCO_UNAVAILABLE;
        table_ = t;
        return SCO_OK;
    }
    sco_result SetBool(const char* name, bool value) const noexcept {
        return table_ ? table_->set_bool(self_, name, SC_CVAR_BOOL, value ? 1u : 0u) : SCO_UNAVAILABLE;
    }
    sco_result RestoreAll() const noexcept {
        return table_ ? table_->restore_all(self_) : SCO_UNAVAILABLE;
    }
};
}
