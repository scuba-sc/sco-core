// GPL-3.0-only. Guards complete extents, null methods and failed reopen.
#ifndef SCOSDK_DIAGNOSTICS_PUBLISHER_HPP
#define SCOSDK_DIAGNOSTICS_PUBLISHER_HPP
#include "sco_diagnostics_publisher.h"
#include <cstddef>
namespace sco::sdk {
class DiagnosticsPublisher {
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr; self_ = nullptr;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* out = nullptr;
        const auto r = api->query_service(SCO_DIAGNOSTICS_PUBLISHER_NAME, SCO_DIAGNOSTICS_PUBLISHER_VERSION_1_0, &out);
        if (r != SCO_OK) return r;
        if (!out) return SCO_UNAVAILABLE;
        table_ = static_cast<const sco_diagnostics_publisher_v1*>(out); self_ = self;
        return SCO_OK;
    }
    sco_result Submit(const sco_diagnostics_observation_v1& observation) const noexcept {
        if (!table_ || table_->size < offsetof(sco_diagnostics_publisher_v1, submit) + sizeof(table_->submit) || !table_->submit)
            return SCO_UNAVAILABLE;
        return table_->submit(self_, &observation);
    }
private:
    const sco_diagnostics_publisher_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
};
}
#endif
