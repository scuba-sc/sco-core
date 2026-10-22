// SPDX-License-Identifier: GPL-3.0-only
#include "../src/game/services/actors.h"
#include "sco/host.h"
#include <sc_actor_stats.h>
#include <sc_actor_stats_write.h>
#include <cstdio>
int main() {
    sco::SetGameThread();
    const auto* api = sco::host::BuildApi({"actor read/write boundary fixture"});
    auto* self = sco::host::NewPlugin("condition_write_boundary");
    unsigned failed = 0;
    if (sco::game::services::StartActorStats() != sco::Result::Ok) return 1;
    const void* raw = nullptr;
    if (api->query_service(SC_ACTOR_STATS_WRITE_NAME, SC_ACTOR_STATS_WRITE_VERSION_1_0, &raw) != SCO_OK) return 2;
    const auto* writes = static_cast<const sc_actor_stats_write_v1*>(raw);
    for (uint32_t stat = 10; stat <= SC_ACTOR_STAT_LAST_1_1; ++stat) {
        float after = 99;
        uint32_t mode = 99;
        if (sc_actor_stats_adjust(writes, self, 256, stat, 1, &after) != SCO_BAD_ARG || after != 0) ++failed;
        after = 99;
        if (sc_actor_stats_set(writes, self, 256, stat, 1, &after) != SCO_BAD_ARG || after != 0) ++failed;
        if (sc_actor_stats_write_mode(writes, self, 256, stat, &mode) != SCO_BAD_ARG) ++failed;
    }
    float old = 99;
    if (sc_actor_stats_adjust(writes, self, 256, SC_ACTOR_STAT_HUNGER, 1, &old) != SCO_UNAVAILABLE) ++failed;
    sco::game::services::StopActorStats();
    std::printf("actual actor write boundary: %u failed\n", failed);
    return failed ? 1 : 0;
}
