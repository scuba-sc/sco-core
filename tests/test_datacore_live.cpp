// Synthetic public-service/provider tests for datacore.live
#include "sc_datacore_live.h"
#include "sco/host.h"
#include "../src/api/internal.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sco::game::services::datacore_live {
    sco_result Start();
    void Stop();
    void SetRecordForTesting(const uint8_t guid[16], uint32_t struct_type, uint32_t field_count, uint64_t pool_offset);
}

int main() {
    using namespace sco::game::services::datacore_live;
    sco::SetGameThread();

    const auto* api = sco::host::BuildApi({"datacore-live-test"});
    assert(Start() == SCO_OK);

    const void* table_ptr = nullptr;
    assert(api->query_service(
        SC_DATACORE_LIVE_SERVICE_NAME,
        SC_DATACORE_LIVE_SERVICE_VERSION,
        &table_ptr
    ) == SCO_OK);
    assert(table_ptr != nullptr);
    const auto* dc = static_cast<const sc_datacore_live_v1*>(table_ptr);
    auto* self = sco::host::NewPlugin("datacore_live_tester");

    // 1. is_ready
    assert(dc->is_ready(self) == SCO_OK);
    assert(sc_datacore_live_is_ready(dc, self) == SCO_OK);

    // 2. get_pool_stats
    sc_datacore_pool_stats stats{};
    stats.size = sizeof(stats);
    assert(dc->get_pool_stats(self, SC_DC_POOL_FLOAT, &stats) == SCO_OK);
    assert(stats.pool_type == SC_DC_POOL_FLOAT);
    assert(stats.element_size == 4);
    assert(stats.element_count > 0);

    // Bad args check
    assert(dc->get_pool_stats(self, SC_DC_POOL_COUNT, &stats) == SCO_BAD_ARG);
    assert(dc->get_pool_stats(self, SC_DC_POOL_FLOAT, nullptr) == SCO_BAD_ARG);

    // 3. write_pool & read_pool
    float write_val = 123.456f;
    size_t written = 0;
    assert(dc->write_pool(self, SC_DC_POOL_FLOAT, 0, &write_val, sizeof(write_val), &written) == SCO_OK);
    assert(written == sizeof(write_val));

    float read_val = 0.0f;
    size_t read_bytes = 0;
    assert(dc->read_pool(self, SC_DC_POOL_FLOAT, 0, &read_val, sizeof(read_val), &read_bytes) == SCO_OK);
    assert(read_bytes == sizeof(read_val));
    assert(read_val == write_val);

    // 4. find_record
    uint8_t dummy_guid[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    SetRecordForTesting(dummy_guid, 42, 5, 0x100);

    sc_datacore_live_record_info rec{};
    rec.size = sizeof(rec);
    assert(dc->find_record(self, dummy_guid, &rec) == SCO_OK);
    assert(rec.struct_type == 42);
    assert(rec.field_count == 5);
    assert(rec.pool_offset == 0x100);

    uint8_t unknown_guid[16] = {0};
    assert(dc->find_record(self, unknown_guid, &rec) == SCO_NOT_FOUND);

    assert(sco::Release(self) == sco::Result::Ok);
    Stop();
    std::puts("PASS test_datacore_live");
    return 0;
}
