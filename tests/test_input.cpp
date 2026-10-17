// SPDX-License-Identifier: GPL-3.0-only
#include "../src/game/services/input.h"
#include "sc_input.h"
#include "sco/host.h"
#include "../src/api/internal.h"
#include <cstdio>
#include <cstring>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"input registry fixture"});
    sco_plugin* self = sco::host::NewPlugin("input_test");
    CHECK(sco::game::services::StartInput() == sco::Result::Ok);
    const void* p = nullptr;
    CHECK(api->query_service(SC_INPUT_SERVICE_NAME, SC_INPUT_SERVICE_VERSION, &p) == SCO_OK);
    const auto* t = static_cast<const sc_input_v1*>(p);
    uint32_t count = 0;
    CHECK(t->count(self, &count) == SCO_OK && count == 1267);
    CHECK(t->count(nullptr, &count) == SCO_BAD_ARG);
    CHECK(t->count(self, nullptr) == SCO_BAD_ARG);
    for (uint32_t id = 0; id <= 1265; ++id) {
        sc_input_action_info a{}; a.size = sizeof(a);
        sc_input_action_info b{}; b.size = sizeof(b);
        CHECK(t->by_id(self, id, &a) == SCO_OK && a.id == id);
        CHECK(t->by_name(self, a.name, &b) == SCO_OK && b.id == id && std::strcmp(a.name, b.name) == 0);
        CHECK(((a.flags & SC_INPUT_ACTION_BUILD_UNRESOLVED) != 0) == (id > 186));
    }
    sc_input_action_info a{}; a.size = sizeof(a);
    CHECK(t->by_name(self, "INVALID", &a) == SCO_OK && a.id == SC_ACTION_INVALID);
    CHECK(t->by_name(self, "v_pitch", &a) == SCO_OK && a.id == SC_ACTION_V_PITCH);
    CHECK(t->by_name(self, "attack1", &a) == SCO_OK && a.id == 133);
    const auto saved = a;
    CHECK(t->by_id(self, 1266, &a) == SCO_NOT_FOUND && std::memcmp(&saved, &a, sizeof(a)) == 0);
    CHECK(t->by_id(self, UINT32_MAX, &a) == SCO_NOT_FOUND);
    CHECK(t->by_name(self, "V_PITCH", &a) == SCO_NOT_FOUND);
    CHECK(t->by_name(self, "not_an_action", &a) == SCO_NOT_FOUND);
    CHECK(t->by_name(self, "", &a) == SCO_BAD_ARG);
    CHECK(t->by_name(self, nullptr, &a) == SCO_BAD_ARG);
    char longName[SC_INPUT_ACTION_NAME_BYTES]; std::memset(longName, 'x', sizeof(longName));
    CHECK(t->by_name(self, longName, &a) == SCO_BAD_ARG);
    a.size = sizeof(a) - 1;
    CHECK(t->by_id(self, 1, &a) == SCO_BAD_ARG);
    sco_result other = SCO_OK;
    std::thread worker([&] { other = t->count(self, &count); }); worker.join();
    CHECK(other == SCO_WRONG_THREAD);
    sco::detail::SetRuntimeStopping(true);
    CHECK(t->count(self, &count) == SCO_UNAVAILABLE);
    sco::detail::SetRuntimeStopping(false);
    sco_plugin* released = sco::host::NewPlugin("input_released");
    sco::Release(released);
    CHECK(t->count(released, &count) == SCO_BAD_ARG);
    sco::game::services::StopInput();
    CHECK(api->query_service(SC_INPUT_SERVICE_NAME, SC_INPUT_SERVICE_VERSION, &p) == SCO_NOT_FOUND);
    CHECK(t->count(self, &count) == SCO_UNAVAILABLE);
    std::printf("input registry: 1267 named members, 1266 numeric ids, all roundtrips and bounds passed; 0 failed\n");
    return 0;
}
