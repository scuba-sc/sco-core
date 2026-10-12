// GPL-3.0-only. Optional C++ guards over the public sco.content C table.
#ifndef SCOSDK_CONTENT_HPP
#define SCOSDK_CONTENT_HPP
#include "sco_content.h"
#include <cstddef>
namespace sco::sdk {
class Content {
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        table_ = nullptr; self_ = nullptr;
        if (!api || !self) return SCO_BAD_ARG;
        if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* out = nullptr;
        const auto r = api->query_service(SCO_CONTENT_NAME, SCO_CONTENT_VERSION_1_0, &out);
        if (r != SCO_OK) return r;
        if (!out) return SCO_UNAVAILABLE;
        table_ = static_cast<const sco_content_v1*>(out);
        self_ = self;
        return SCO_OK;
    }
    sco_result Item(sco_content_kind kind, uint32_t ordinal, sco_content_item& out) const noexcept {
        if (!table_ || table_->size < offsetof(sco_content_v1, item) + sizeof(table_->item) || !table_->item)
            return SCO_UNAVAILABLE;
        return table_->item(self_, kind, ordinal, &out);
    }
    sco_result Read(uint64_t id, void* out, uint32_t& size) const noexcept {
        if (!table_ || table_->size < offsetof(sco_content_v1, read) + sizeof(table_->read) || !table_->read)
            return SCO_UNAVAILABLE;
        return table_->read(self_, id, out, &size);
    }
    void Reset() noexcept { table_ = nullptr; self_ = nullptr; }
private:
    const sco_content_v1* table_ = nullptr;
    sco_plugin* self_ = nullptr;
};
}
#endif
