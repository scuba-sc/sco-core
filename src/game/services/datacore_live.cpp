// SPDX-License-Identifier: GPL-3.0-only
#include "sc_datacore_live.h"
#include "sco_api.h"
#include "sco/host.h"
#include "sco/runtime.h"

#include <algorithm>
#include <cstring>
#include <vector>
#include <map>
#include <mutex>

namespace sco::game::services::datacore_live {
namespace {

struct LivePool {
    uint32_t type;
    size_t element_size;
    std::vector<uint8_t> data;
};

std::mutex g_lock;
bool g_published = false;
bool g_ready = true;
std::vector<LivePool> g_pools;
std::map<std::vector<uint8_t>, sc_datacore_live_record_info> g_records;

void InitDefaultPoolsLocked() {
    g_pools.resize(SC_DC_POOL_COUNT);
    const size_t element_sizes[SC_DC_POOL_COUNT] = {
        1, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 16, 8
    };
    for (size_t i = 0; i < SC_DC_POOL_COUNT; ++i) {
        g_pools[i].type = static_cast<uint32_t>(i);
        g_pools[i].element_size = element_sizes[i];
        g_pools[i].data.assign(element_sizes[i] * 64, 0); // initial synthetic storage
    }
}

sco_result IsReady(sco_plugin* self) {
    (void)self;
    std::lock_guard<std::mutex> lock(g_lock);
    return g_ready ? SCO_OK : SCO_UNAVAILABLE;
}

sco_result GetPoolStats(sco_plugin* self, uint32_t pool_type, sc_datacore_pool_stats* out_stats) {
    (void)self;
    if (!out_stats || out_stats->size < sizeof(sc_datacore_pool_stats) || pool_type >= SC_DC_POOL_COUNT) {
        return SCO_BAD_ARG;
    }
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_ready) return SCO_UNAVAILABLE;

    const auto& p = g_pools[pool_type];
    out_stats->pool_type = pool_type;
    out_stats->element_size = p.element_size;
    out_stats->element_count = p.data.size() / p.element_size;
    out_stats->capacity = out_stats->element_count;
    out_stats->total_bytes = p.data.size();
    return SCO_OK;
}

sco_result FindRecord(sco_plugin* self, const uint8_t guid[16], sc_datacore_live_record_info* out_record) {
    (void)self;
    if (!guid || !out_record || out_record->size < sizeof(sc_datacore_live_record_info)) {
        return SCO_BAD_ARG;
    }
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_ready) return SCO_UNAVAILABLE;

    std::vector<uint8_t> key(guid, guid + 16);
    auto it = g_records.find(key);
    if (it == g_records.end()) return SCO_NOT_FOUND;

    *out_record = it->second;
    return SCO_OK;
}

sco_result ReadPool(sco_plugin* self, uint32_t pool_type, uint64_t offset, void* out_buffer, size_t buffer_size, size_t* out_read) {
    (void)self;
    if (!out_buffer || buffer_size == 0 || pool_type >= SC_DC_POOL_COUNT) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_ready) return SCO_UNAVAILABLE;

    const auto& p = g_pools[pool_type];
    if (offset >= p.data.size()) return SCO_BAD_ARG;

    const size_t to_read = std::min(buffer_size, p.data.size() - static_cast<size_t>(offset));
    std::memcpy(out_buffer, p.data.data() + offset, to_read);
    if (out_read) *out_read = to_read;
    return SCO_OK;
}

sco_result WritePool(sco_plugin* self, uint32_t pool_type, uint64_t offset, const void* in_buffer, size_t buffer_size, size_t* out_written) {
    (void)self;
    if (!in_buffer || buffer_size == 0 || pool_type >= SC_DC_POOL_COUNT) return SCO_BAD_ARG;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_ready) return SCO_UNAVAILABLE;

    auto& p = g_pools[pool_type];
    if (offset >= p.data.size()) return SCO_BAD_ARG;

    const size_t to_write = std::min(buffer_size, p.data.size() - static_cast<size_t>(offset));
    std::memcpy(p.data.data() + offset, in_buffer, to_write);
    if (out_written) *out_written = to_write;
    return SCO_OK;
}

const sc_datacore_live_v1 kTable = {
    sizeof(sc_datacore_live_v1),
    0,
    IsReady,
    GetPoolStats,
    FindRecord,
    ReadPool,
    WritePool
};

} // namespace

sco_result Start() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_published) return SCO_FAILED;
    InitDefaultPoolsLocked();
    const sco::Result r = sco::host::ProvideGameService(
        SC_DATACORE_LIVE_SERVICE_NAME,
        SC_DATACORE_LIVE_SERVICE_VERSION,
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
    sco::host::WithdrawGameService(SC_DATACORE_LIVE_SERVICE_NAME);
    g_published = false;
    g_pools.clear();
    g_records.clear();
}

void SetRecordForTesting(const uint8_t guid[16], uint32_t struct_type, uint32_t field_count, uint64_t pool_offset) {
    std::lock_guard<std::mutex> lock(g_lock);
    sc_datacore_live_record_info info{};
    info.size = sizeof(info);
    std::memcpy(info.guid, guid, 16);
    info.struct_type = struct_type;
    info.field_count = field_count;
    info.pool_offset = pool_offset;
    g_records[std::vector<uint8_t>(guid, guid + 16)] = info;
}

} // namespace sco::game::services::datacore_live
