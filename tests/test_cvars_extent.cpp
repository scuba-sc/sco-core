// SPDX-License-Identifier: GPL-3.0-only
#include <scosdk/game/cvars.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static const void* raw = nullptr;
static sco_result query = SCO_OK;
static sco_result Query(const char*, uint32_t, const void** out) { *out = raw; return query; }
static sco_result Get(sco_plugin*, const char*, uint32_t, uint32_t* out) { *out = 1; return SCO_OK; }
int main() {
    sco_api api{}; api.size = sizeof(api); api.query_service = Query;
    scosdk::game::Cvars ref(&api, nullptr);
    unsigned failed = 0, passed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
    uint32_t out = 9;
    for (uint32_t n = 8; n < sizeof(sc_cvars_v1); ++n) {
        void* p = std::malloc(n); std::memset(p, 0xff, n); std::memcpy(p, &n, sizeof(n)); raw = p;
        CHECK(ref.Open() == SCO_UNAVAILABLE);
        CHECK(ref.GetBool("g_squadron_frontend", out) == SCO_UNAVAILABLE && out == 9);
        std::free(p);
    }
    sc_cvars_v1 table{sizeof(table), 0, nullptr}; raw = &table;
    CHECK(ref.Open() == SCO_UNAVAILABLE);
    table.get_bool = Get; CHECK(ref.Open() == SCO_OK);
    CHECK(ref.GetBool("g_squadron_frontend", out) == SCO_OK && out == 1);
    query = SCO_NOT_FOUND; CHECK(ref.Open() == SCO_NOT_FOUND);
    CHECK(ref.GetBool("g_squadron_frontend", out) == SCO_UNAVAILABLE);
    raw = nullptr; query = SCO_OK; CHECK(ref.Open() == SCO_UNAVAILABLE);
    std::printf("cvars extent: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
