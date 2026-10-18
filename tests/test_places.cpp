// SPDX-License-Identifier: GPL-3.0-only
#include "sc_places.h"
#include "sco/host.h"
#include "../src/api/internal.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>

namespace sco::game::services::places {
    sco_result Start();
    void Stop();
}

int main() {
    using namespace sco::game::services::places;
    sco::SetGameThread();

    const auto* api = sco::host::BuildApi({"places-test"});
    assert(Start() == SCO_OK);

    const void* table_ptr = nullptr;
    assert(api->query_service(
        SC_PLACES_SERVICE_NAME,
        SC_PLACES_SERVICE_VERSION,
        &table_ptr
    ) == SCO_OK);
    assert(table_ptr != nullptr);
    const auto* p = static_cast<const sc_places_v1*>(table_ptr);
    auto* self = sco::host::NewPlugin("places_tester");

    // 1. count
    uint32_t count = 0;
    assert(p->count(self, &count) == SCO_OK);
    assert(count == 5);
    assert(sc_places_count(p, self, &count) == SCO_OK);
    assert(p->count(self, nullptr) == SCO_BAD_ARG);

    // 2. by_index
    sc_place_info info{};
    info.size = sizeof(info);
    assert(p->by_index(self, 0, &info) == SCO_OK);
    assert(std::strcmp(info.name, "Stanton") == 0);
    assert(info.place_id == 1);
    assert(info.parent_place_id == 0);

    // Out of bounds index
    assert(p->by_index(self, 999, &info) == SCO_NOT_FOUND);

    // 3. by_name
    assert(p->by_name(self, "Lorville", &info) == SCO_OK);
    assert(info.place_id == 3);
    assert(info.parent_place_id == 2);
    assert(info.zone_id == 0x4);

    assert(p->by_name(self, "NonExistentPlace", &info) == SCO_NOT_FOUND);

    // 4. by_id
    assert(p->by_id(self, 4, &info) == SCO_OK);
    assert(std::strcmp(info.name, "Crusader") == 0);
    assert(p->by_id(self, 9999, &info) == SCO_NOT_FOUND);

    assert(sco::Release(self) == sco::Result::Ok);
    Stop();
    std::puts("PASS test_places");
    return 0;
}
