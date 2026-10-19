// SPDX-License-Identifier: GPL-3.0-only
// Synthetic storage exists only in this fixture, never in the production provider.
#include "../src/game/services/datacore_live.h"
#include "sco/host.h"
#include "../src/api/internal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <stdexcept>

namespace {
bool offline = true, ready = true, fault = false;
unsigned reads = 0, writes = 0;
float value = 12.5f;
bool Offline() { return offline; }
bool Ready() { if (fault) throw std::runtime_error("fixture"); return ready; }
sco_result Stats(uint32_t type, sc_datacore_pool_stats* out) {
    *out = {sizeof(*out), type, 1, 1, sizeof(value), sizeof(value)}; return SCO_OK;
}
sco_result Find(const uint8_t[16], sc_datacore_live_record_info*) { return SCO_NOT_FOUND; }
sco_result Read(uint32_t, uint64_t offset, void* out, size_t bytes, size_t* count) {
    ++reads; if (offset || bytes != sizeof(value)) return SCO_BAD_ARG;
    std::memcpy(out, &value, bytes); *count = bytes; return SCO_OK;
}
sco_result Write(uint32_t, uint64_t offset, const void* in, size_t bytes, size_t* count) {
    ++writes; if (offset || bytes != sizeof(value)) return SCO_BAD_ARG;
    std::memcpy(&value, in, bytes); *count = bytes; return SCO_OK;
}
}
int main() {
    using namespace sco::game::services::datacore_live;
    sco::SetGameThread();
    const auto* api = sco::host::BuildApi({"datacore-live-test"});
    auto* self = sco::host::NewPlugin("datacore_live_tester");
    assert(Start() == SCO_OK);
    const void* table = nullptr;
    assert(api->query_service(SC_DATACORE_LIVE_SERVICE_NAME, SC_DATACORE_LIVE_SERVICE_VERSION, &table) == SCO_OK);
    const auto* dc = static_cast<const sc_datacore_live_v1*>(table);
    assert(dc->is_ready(self) == SCO_UNAVAILABLE);
    Backend backend{true, Offline, Ready, Stats, Find, Read, Write};
    assert(Start(&backend) == SCO_UNAVAILABLE); // no silent replacement
    Stop();
    backend.verified = false;
    assert(Start(&backend) == SCO_OK && dc->is_ready(self) == SCO_UNAVAILABLE);
    Stop(); backend.verified = true;
    auto incomplete = backend; incomplete.read = nullptr;
    assert(Start(&incomplete) == SCO_OK && dc->is_ready(self) == SCO_UNAVAILABLE);
    Stop(); assert(Start(&backend) == SCO_OK);
    assert(sc_datacore_live_is_ready(dc, self) == SCO_OK);
    auto* released = sco::host::NewPlugin("datacore_released");
    assert(sco::Release(released) == sco::Result::Ok);
    assert(dc->is_ready(released) == SCO_BAD_ARG);
    assert(dc->is_ready(nullptr) == SCO_BAD_ARG);
    sco_result threadResult = SCO_OK;
    std::thread other([&] { threadResult = dc->is_ready(self); }); other.join();
    assert(threadResult == SCO_WRONG_THREAD);
    float out = 0, replacement = 42;
    size_t count = 0;
    assert(dc->read_pool(self, SC_DC_POOL_FLOAT, 0, &out, sizeof(out), &count) == SCO_OK);
    assert(out == value && count == sizeof(out) && reads == 1);
    assert(dc->write_pool(self, SC_DC_POOL_FLOAT, 0, &replacement, sizeof(replacement), &count) == SCO_UNAVAILABLE && writes == 0);
    SetWritesEnabled(true);
    assert(dc->write_pool(self, SC_DC_POOL_FLOAT, 0, &replacement, sizeof(replacement), &count) == SCO_OK && writes == 1);
    assert(dc->read_pool(self, SC_DC_POOL_FLOAT, UINT64_MAX, &out, sizeof(out), &count) == SCO_BAD_ARG && count == 0);
    sc_datacore_pool_stats stats{};
    stats.size = sizeof(stats);
    assert(dc->get_pool_stats(self, SC_DC_POOL_FLOAT, &stats) == SCO_OK && stats.element_count == 1);
    stats.size = 4;
    assert(dc->get_pool_stats(self, SC_DC_POOL_FLOAT, &stats) == SCO_BAD_ARG);
    uint8_t guid[16]{}; sc_datacore_live_record_info record{};
    record.size = sizeof(record);
    assert(dc->find_record(self, guid, &record) == SCO_NOT_FOUND);
    offline = false;
    const auto before = writes;
    assert(dc->write_pool(self, SC_DC_POOL_FLOAT, 0, &replacement, sizeof(replacement), &count) == SCO_UNAVAILABLE && writes == before);
    offline = true; ready = false;
    assert(dc->is_ready(self) == SCO_UNAVAILABLE);
    ready = true; fault = true;
    assert(dc->is_ready(self) == SCO_FAILED);
    fault = false;
    assert(dc->is_ready(self) == SCO_UNAVAILABLE); // process-lifetime fault latch
    Stop(); assert(dc->is_ready(self) == SCO_UNAVAILABLE);
    table = nullptr;
    assert(api->query_service(SC_DATACORE_LIVE_SERVICE_NAME, SC_DATACORE_LIVE_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    assert(sco::Release(self) == sco::Result::Ok);
    std::puts("PASS test_datacore_live: explicit backend, no synthetic production storage");
}
