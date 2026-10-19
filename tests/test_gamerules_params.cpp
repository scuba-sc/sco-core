// SPDX-License-Identifier: GPL-3.0-only
// game.gamerules 1.1 list_params: the static parameter-definition registry (F-INTAKE-005 member
// names, no values). Real host API and service start/stop; nothing here runs in game.
#include <sc_gamerules.h>
#include "../src/game/services/gamerules.h"
#include "sco/host.h"
#include "../src/api/internal.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <stdexcept>
using sco::Result;
namespace G = sco::game::services;
static int pass = 0, fail = 0;
#define CHECK(x) do { if (x) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

struct Expect { uint32_t type; const char* group; const char* name; };
// Declaration order of SGameRulesCvars, nested structs flattened where they are declared.
static const Expect kExpect[] = {
    {SC_GAMERULES_PARAM_INT, "control", "capturableDebug"},
    {SC_GAMERULES_PARAM_INT, "control", "phaseDebug"},
    {SC_GAMERULES_PARAM_INT, "control", "objectiveDebug"},
    {SC_GAMERULES_PARAM_FLOAT, "control", "terminalsDisabledTime"},
    {SC_GAMERULES_PARAM_INT, "state", "log"},
    {SC_GAMERULES_PARAM_INT, "scoring", "enabled"},
    {SC_GAMERULES_PARAM_INT, "scoring", "log"},
    {SC_GAMERULES_PARAM_INT, "damagehandling", "log"},
    {SC_GAMERULES_PARAM_INT, "damagehandling", "disableCollisionFilter"},
    {SC_GAMERULES_PARAM_INT, "accessibility", "QTEMode"},
    {SC_GAMERULES_PARAM_INT, "", "showRaceCheckpoints"},
    {SC_GAMERULES_PARAM_INT, "", "showRaceInfo"},
    {SC_GAMERULES_PARAM_FLOAT, "", "overrideRespawnTimer"},
    {SC_GAMERULES_PARAM_INT, "", "editorUseSpawnpoints"},
    {SC_GAMERULES_PARAM_INT, "", "allowNonLocationSpawnpoints"},
    {SC_GAMERULES_PARAM_INT, "", "despawnOnProjectileHit"},
    {SC_GAMERULES_PARAM_INT, "", "allowAllPlayerCameras"},
};
static constexpr uint32_t kTotal = sizeof(kExpect) / sizeof(kExpect[0]);

static unsigned reads = 0;
static bool BackendRead(void*, sc_gamerules_state* out) { ++reads; out->valid = 1; return true; }

// A caller buffer of slots at an arbitrary stride, pre-filled with a sentinel byte.
struct Buf {
    std::vector<unsigned char> bytes;
    uint32_t stride, cap;
    Buf(uint32_t stride_, uint32_t cap_, uint32_t advertised = sizeof(sc_gamerules_param_info))
        : bytes(static_cast<size_t>(stride_) * cap_, 0xA5), stride(stride_), cap(cap_) {
        for (uint32_t i = 0; i != cap_; ++i) std::memcpy(&bytes[static_cast<size_t>(i) * stride_], &advertised, 4);
    }
    sc_gamerules_param_info* out() { return reinterpret_cast<sc_gamerules_param_info*>(bytes.data()); }
    sc_gamerules_param_info rec(uint32_t i) const {
        sc_gamerules_param_info r; std::memcpy(&r, &bytes[static_cast<size_t>(i) * stride], sizeof(r)); return r;
    }
    bool untouched(uint32_t advertised = sizeof(sc_gamerules_param_info)) const {
        for (uint32_t i = 0; i != cap; ++i)
            for (uint32_t b = 0; b != stride; ++b) {
                const unsigned char want = b < 4 ? static_cast<unsigned char>((advertised >> (8 * b)) & 0xFF) : 0xA5;
                if (bytes[static_cast<size_t>(i) * stride + b] != want) return false;
            }
        return true;
    }
};
static bool Matches(const sc_gamerules_param_info& r, uint32_t index) {
    const Expect& e = kExpect[index];
    return r.size == sizeof(sc_gamerules_param_info) && r.type == e.type && std::strcmp(r.name, e.name) == 0 &&
           std::strcmp(r.group, e.group) == 0 && std::memchr(r.name, 0, sizeof(r.name)) &&
           std::memchr(r.group, 0, sizeof(r.group));
}

int main() {
    sco::SetGameThread();
    sco_plugin* self = sco::host::NewPlugin("gamerules_params_test");
    const auto* api = sco::host::BuildApi({"synthetic gamerules params"});
    const void* raw = nullptr;

    // Not published before start.
    CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_0, &raw) == SCO_NOT_FOUND);

    // Default production start: no backend. The registry is available, read is not.
    CHECK(G::StartGameRules() == Result::Ok);
    CHECK(G::StartGameRules() == Result::Failed);
    CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1, &raw) == SCO_OK);
    const auto* t = static_cast<const sc_gamerules_v1*>(raw);
    CHECK(t && t->size == sizeof(sc_gamerules_v1) && t->read && t->list_params);
    const void* old_raw = nullptr;  // old read-only callers query 1.0 and get the same table
    CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_SERVICE_VERSION, &old_raw) == SCO_OK);
    CHECK(old_raw == raw);
    CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, 0x00010002u, &old_raw) != SCO_OK);
    sc_gamerules_state st; std::memset(&st, 0, sizeof(st)); st.size = sizeof(st);
    CHECK(sc_gamerules_read(t, self, &st) == SCO_UNAVAILABLE && st.valid == 0 && st.size == sizeof(st));
    // The old caller record (16 bytes, read only) keeps working against the 1.1 table's prefix.
    sc_gamerules_v1 legacy{}; std::memcpy(&legacy, t, offsetof(sc_gamerules_v1, read) + sizeof(t->read));
    legacy.size = static_cast<uint32_t>(offsetof(sc_gamerules_v1, read) + sizeof(t->read));
    CHECK(legacy.size == 16 && legacy.read == t->read);
    CHECK(sc_gamerules_read(&legacy, self, &st) == SCO_UNAVAILABLE);
    uint32_t cnt = 77, total = 77;
    CHECK(sc_gamerules_list_params(&legacy, self, 0, nullptr, 0, 0, &cnt, &total) == SCO_UNAVAILABLE);
    CHECK(cnt == 77 && total == 77);

    // Count only, and offset == total (empty page): count 0, total is the registry size.
    CHECK(sc_gamerules_list_params(t, self, 0, nullptr, 0, 0, &cnt, &total) == SCO_OK && cnt == 0 && total == kTotal);
    cnt = total = 77;
    CHECK(sc_gamerules_list_params(t, self, kTotal, nullptr, 0, 0, &cnt, &total) == SCO_OK && cnt == 0 && total == kTotal);
    {
        Buf b(sizeof(sc_gamerules_param_info), 4);
        cnt = total = 77;
        CHECK(t->list_params(self, kTotal, b.out(), b.stride, b.cap, &cnt, &total) == SCO_OK);
        CHECK(cnt == 0 && total == kTotal && b.untouched());
    }

    // Full enumeration at the exact stride, in registry order; complete = offset + count == total.
    {
        Buf b(sizeof(sc_gamerules_param_info), 32);
        cnt = total = 77;
        CHECK(t->list_params(self, 0, b.out(), b.stride, b.cap, &cnt, &total) == SCO_OK);
        CHECK(cnt == kTotal && total == kTotal);
        for (uint32_t i = 0; i != kTotal; ++i) CHECK(Matches(b.rec(i), i));
        // Slots past the count are untouched.
        for (uint32_t i = kTotal; i != 32; ++i) {
            const auto r = b.rec(i);
            CHECK(r.size == sizeof(sc_gamerules_param_info) && r.type == 0xA5A5A5A5u);
        }
    }
    // Wider stride and a larger advertised size: known fields only, caller size and tail kept.
    {
        const uint32_t advertised = sizeof(sc_gamerules_param_info) + 8;
        Buf b(sizeof(sc_gamerules_param_info) + 16, 3, advertised);
        CHECK(t->list_params(self, 2, b.out(), b.stride, b.cap, &cnt, &total) == SCO_OK && cnt == 3 && total == kTotal);
        for (uint32_t i = 0; i != 3; ++i) {
            const auto r = b.rec(i);
            CHECK(r.size == advertised && r.type == kExpect[2 + i].type &&
                  std::strcmp(r.name, kExpect[2 + i].name) == 0 && std::strcmp(r.group, kExpect[2 + i].group) == 0);
            for (uint32_t k = sizeof(sc_gamerules_param_info); k != b.stride; ++k)
                CHECK(b.bytes[static_cast<size_t>(i) * b.stride + k] == 0xA5);
        }
    }
    // Unaligned stride is allowed; fields are copied, not cast.
    {
        Buf b(sizeof(sc_gamerules_param_info) + 1, 2);
        CHECK(t->list_params(self, 0, b.out(), b.stride, b.cap, &cnt, &total) == SCO_OK && cnt == 2);
        CHECK(Matches(b.rec(0), 0) && Matches(b.rec(1), 1));
    }
    // Paged walk with an explicit offset is the same sequence as the full list.
    {
        uint32_t seen = 0, offset = 0, pages = 0;
        total = kTotal;
        while (offset < total && pages < 16) {
            Buf b(sizeof(sc_gamerules_param_info), 5);
            CHECK(t->list_params(self, offset, b.out(), b.stride, b.cap, &cnt, &total) == SCO_OK);
            CHECK(total == kTotal && cnt != 0);
            for (uint32_t i = 0; i != cnt; ++i) CHECK(Matches(b.rec(i), offset + i));
            seen += cnt; offset += cnt; ++pages;
        }
        CHECK(seen == kTotal && offset == total && pages == (kTotal + 4) / 5);
    }
    // Short page and cap boundaries: cap 1, cap == remaining, cap == max page.
    {
        Buf one(sizeof(sc_gamerules_param_info), 1);
        CHECK(t->list_params(self, kTotal - 1, one.out(), one.stride, 1, &cnt, &total) == SCO_OK && cnt == 1);
        CHECK(Matches(one.rec(0), kTotal - 1));
        Buf big(sizeof(sc_gamerules_param_info), SC_GAMERULES_PARAM_MAX_PAGE);
        CHECK(t->list_params(self, 3, big.out(), big.stride, big.cap, &cnt, &total) == SCO_OK && cnt == kTotal - 3);
        Buf wide(SC_GAMERULES_PARAM_MAX_STRIDE, 2);
        CHECK(t->list_params(self, 0, wide.out(), wide.stride, 2, &cnt, &total) == SCO_OK && cnt == 2);
    }

    // Malformed calls are refused and leave every output (buffer and counters) unchanged.
    {
        Buf b(sizeof(sc_gamerules_param_info), 4);
        auto call = [&](sco_plugin* s, uint32_t off, sc_gamerules_param_info* o, uint32_t stride, uint32_t cap,
                        uint32_t* c, uint32_t* tt) { return t->list_params(s, off, o, stride, cap, c, tt); };
        cnt = total = 77;
        CHECK(call(self, kTotal + 1, b.out(), b.stride, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, nullptr, b.stride, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), b.stride, 0, &cnt, &total) == SCO_BAD_ARG);  // cap 0 needs out NULL
        CHECK(call(self, 0, b.out(), sizeof(sc_gamerules_param_info) - 1, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), 0, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), SC_GAMERULES_PARAM_MAX_STRIDE + 1, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), b.stride, SC_GAMERULES_PARAM_MAX_PAGE + 1, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), b.stride, 0xFFFFFFFFu, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), b.stride, 4, nullptr, &total) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), b.stride, 4, &cnt, nullptr) == SCO_BAD_ARG);
        CHECK(call(self, 0, b.out(), b.stride, 4, &cnt, &cnt) == SCO_BAD_ARG);
        CHECK(call(nullptr, 0, b.out(), b.stride, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(call(reinterpret_cast<sco_plugin*>(1), 0, b.out(), b.stride, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(b.untouched() && cnt == 77 && total == 77);
        // A too-small advertised size in the last slot that would be written refuses the whole call,
        // including the valid slots before it.
        Buf s(sizeof(sc_gamerules_param_info), 4);
        const uint32_t small = sizeof(sc_gamerules_param_info) - 1;
        std::memcpy(&s.bytes[3 * s.stride], &small, 4);
        CHECK(t->list_params(self, 0, s.out(), s.stride, 4, &cnt, &total) == SCO_BAD_ARG);
        CHECK(cnt == 77 && total == 77);
        for (uint32_t i = 0; i != 3; ++i) CHECK(s.rec(i).type == 0xA5A5A5A5u);
        // A short slot beyond the written range is not examined: offset leaves only slot 0 to fill.
        CHECK(t->list_params(self, kTotal - 1, s.out(), s.stride, 4, &cnt, &total) == SCO_OK && cnt == 1);
        // Wrong thread.
        Buf w(sizeof(sc_gamerules_param_info), 2);
        sco_result off_thread = SCO_OK; cnt = total = 77;
        std::thread worker([&] { off_thread = t->list_params(self, 0, w.out(), w.stride, 2, &cnt, &total); });
        worker.join();
        CHECK(off_thread == SCO_WRONG_THREAD && w.untouched() && cnt == 77 && total == 77);
    }

    // Runtime stopping: structurally valid calls are unavailable and write nothing.
    {
        Buf b(sizeof(sc_gamerules_param_info), 2);
        cnt = total = 77;
        sco::detail::SetRuntimeStopping(true);
        CHECK(t->list_params(self, 0, b.out(), b.stride, 2, &cnt, &total) == SCO_UNAVAILABLE);
        sco::detail::SetRuntimeStopping(false);
        CHECK(b.untouched() && cnt == 77 && total == 77);
        CHECK(t->list_params(self, 0, b.out(), b.stride, 2, &cnt, &total) == SCO_OK && cnt == 2);
    }

    // The registry never reaches a backend and never changes read. Bind one and compare.
    G::StopGameRules();
    CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_0, &raw) == SCO_NOT_FOUND);
    CHECK(t->list_params == nullptr && t->read == nullptr);
    {
        G::GameRulesBackend backend; backend.read = BackendRead;
        CHECK(G::StartGameRules(backend) == Result::Ok);
        CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1, &raw) == SCO_OK);
        t = static_cast<const sc_gamerules_v1*>(raw);
        Buf b(sizeof(sc_gamerules_param_info), 32);
        CHECK(t->list_params(self, 0, b.out(), b.stride, 32, &cnt, &total) == SCO_OK && cnt == kTotal);
        CHECK(reads == 0);
        CHECK(sc_gamerules_read(t, self, &st) == SCO_OK && st.valid == 1 && st.size == sizeof(st) && reads == 1);
        CHECK(t->list_params(self, 0, nullptr, 0, 0, &cnt, &total) == SCO_OK && reads == 1);
        sco_result otherThread = SCO_OK;
        std::thread([&] { otherThread = t->read(self, &st); }).join();
        CHECK(otherThread == SCO_WRONG_THREAD && reads == 1);
        CHECK(t->read(nullptr, &st) == SCO_BAD_ARG && reads == 1);
        auto* released = sco::host::NewPlugin("gamerules_released");
        CHECK(sco::Release(released) == Result::Ok);
        CHECK(t->read(released, &st) == SCO_BAD_ARG && reads == 1);
    }

    // A function pointer cached before stop is refused after it, with outputs unchanged.
    auto cached = t->list_params;
    G::StopGameRules();
    G::StopGameRules();  // idempotent
    {
        Buf b(sizeof(sc_gamerules_param_info), 2);
        cnt = total = 77;
        CHECK(cached(self, 0, b.out(), b.stride, 2, &cnt, &total) == SCO_UNAVAILABLE);
        CHECK(b.untouched() && cnt == 77 && total == 77);
        CHECK(sc_gamerules_list_params(t, self, 0, b.out(), b.stride, 2, &cnt, &total) == SCO_UNAVAILABLE);
        CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_0, &raw) == SCO_NOT_FOUND);
    }
    // Restart publishes the registry again.
    CHECK(G::StartGameRules() == Result::Ok);
    CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1, &raw) == SCO_OK);
    CHECK(sc_gamerules_list_params(static_cast<const sc_gamerules_v1*>(raw), self, 0, nullptr, 0, 0, &cnt, &total) ==
              SCO_OK && total == kTotal);
    G::StopGameRules();

    {
        G::GameRulesBackend faulty{};
        faulty.read = [](void*, sc_gamerules_state*) -> bool { throw std::runtime_error("backend fixture"); };
        CHECK(G::StartGameRules(faulty) == Result::Ok);
        CHECK(api->query_service(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1, &raw) == SCO_OK);
        auto* faultTable = static_cast<const sc_gamerules_v1*>(raw);
        bool escaped = false;
        sco_result result = SCO_OK;
        try { result = faultTable->read(self, &st); } catch (...) { escaped = true; }
        CHECK(!escaped && result == SCO_FAILED);
        if (!escaped) CHECK(faultTable->read(self, &st) == SCO_UNAVAILABLE);
        G::StopGameRules();
    }
    std::printf("gamerules params: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
