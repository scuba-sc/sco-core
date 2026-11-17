// SPDX-License-Identifier: GPL-3.0-only
// Copied, owner-scoped sco.storage.metadata 2.0 results. Never opens a database.
#ifndef SCOSDK_STORAGE_METADATA_HPP
#define SCOSDK_STORAGE_METADATA_HPP

#include "service.hpp"
#include "sco_storage_v2.h"
#include <cstring>
#include <string>

namespace sco::sdk {

class StorageMetadata {
public:
    sco_result Open(const Plugin& plugin) noexcept { return Open(plugin.Api(), plugin.Self()); }
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept {
        t_ = nullptr;
        self_ = self;
        if (!api || !self) return SCO_BAD_ARG;
        if (!Covers(api->size, offsetof(sco_api, query_service), sizeof(api->query_service)) || !api->query_service)
            return SCO_UNAVAILABLE;
        const void* table = nullptr;
        const sco_result r = api->query_service(SCO_STORAGE_V2_NAME, SCO_STORAGE_V2_VERSION_2_0, &table);
        if (r == SCO_NOT_FOUND || (r == SCO_OK && !table)) return SCO_UNAVAILABLE;
        if (r != SCO_OK) return r;
        t_ = static_cast<const sco_storage_v2*>(table);
        return SCO_OK;
    }
    explicit operator bool() const noexcept { return t_ != nullptr; }

    // Output arguments change only after a complete, validated successful reply.
    sco_result OwnerInfo(sco_storage_owner_info& out) const noexcept {
        if (!detail::CanCall(t_, &sco_storage_v2::owner_info)) return SCO_UNAVAILABLE;
        sco_storage_owner_info copy{}; copy.size = sizeof(copy);
        const sco_result r = t_->owner_info(self_, &copy);
        if (r != SCO_OK) return r;
        if (copy.size != sizeof(copy) || copy.flags != SCO_STORAGE_OWNER_OPENED || !copy.session_generation)
            return SCO_FAILED;
        out = copy;
        return SCO_OK;
    }
    sco_result NextTable(const char* after, std::string& name, sco_storage_table_info& out) const noexcept {
        if (!detail::CanCall(t_, &sco_storage_v2::next_table)) return SCO_UNAVAILABLE;
        if (after && std::strlen(after) >= MaxNameBuffer) return SCO_BAD_ARG;
        try {
            std::string buffer(256, '\0');
            for (int attempt = 0; attempt < 32; ++attempt) {
                uint32_t size = static_cast<uint32_t>(buffer.size());
                sco_storage_table_info copy{}; copy.size = sizeof(copy);
                const sco_result r = t_->next_table(self_, after, buffer.data(), &size, &copy);
                if (r == SCO_OK) {
                    if (copy.size != sizeof(copy) || copy.flags || !size || size > buffer.size() ||
                        buffer[size - 1] != '\0' || std::memchr(buffer.data(), '\0', size - 1)) return SCO_FAILED;
                    buffer.resize(size - 1);
                    if (after && buffer.compare(after) <= 0) return SCO_FAILED;
                    name.swap(buffer); out = copy;
                    return SCO_OK;
                }
                if (r != SCO_TOO_MANY) return r;
                if (size <= buffer.size() || size > MaxNameBuffer) return SCO_TOO_MANY;
                buffer.assign(size, '\0');
            }
            return SCO_TOO_MANY;
        } catch (...) { return SCO_TOO_MANY; }
    }
    sco_result RowCount(const char* table, uint64_t& out) const noexcept {
        if (!detail::CanCall(t_, &sco_storage_v2::row_count)) return SCO_UNAVAILABLE;
        if (!table || std::strlen(table) >= MaxNameBuffer) return SCO_BAD_ARG;
        uint64_t copy = 0;
        const sco_result r = t_->row_count(self_, table, &copy);
        if (r == SCO_OK) out = copy;
        return r;
    }
    sco_result QuotaInfo(sco_storage_quota_info& out) const noexcept {
        if (!detail::CanCall(t_, &sco_storage_v2::quota_info)) return SCO_UNAVAILABLE;
        sco_storage_quota_info copy{}; copy.size = sizeof(copy);
        const sco_result r = t_->quota_info(self_, &copy);
        if (r != SCO_OK) return r;
        if (copy.size != sizeof(copy) || copy.flags || copy.allocated_bytes > copy.effective_bytes ||
            copy.free_bytes != copy.effective_bytes - copy.allocated_bytes) return SCO_FAILED;
        out = copy;
        return SCO_OK;
    }

private:
    static constexpr uint32_t MaxNameBuffer = 16u * 1024u * 1024u + 1u;
    const sco_storage_v2* t_ = nullptr;
    sco_plugin* self_ = nullptr;
};

} // namespace sco::sdk
#endif // SCOSDK_STORAGE_METADATA_HPP
