/* GPL-3.0-only. New table/record x64 pins and direct C full-extent guards.
 * Compiled as C11; test_game_spatial also exercises raw C++ calls. */
#include "sc_game_spatial.h"
#include "sco_api.h"
#include <stddef.h>
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
extern "C" {
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(void*) == 8);
PIN(sizeof(sco_api) == 128);
PIN(SC_GAME_SPATIAL_VERSION == 0x00010000u);
PIN(sizeof(SC_GAME_SPATIAL_NAME) == 13);
PIN(sizeof(sc_game_spatial_v1) == 40);
PIN(offsetof(sc_game_spatial_v1, size) == 0);
PIN(offsetof(sc_game_spatial_v1, pose_to_zone) == 8);
PIN(offsetof(sc_game_spatial_v1, zone_record) == 16);
PIN(offsetof(sc_game_spatial_v1, find_zone) == 24);
PIN(offsetof(sc_game_spatial_v1, list_zones) == 32);
PIN(sizeof(sc_game_zone) == 144);
PIN(offsetof(sc_game_zone, size) == 0);
PIN(offsetof(sc_game_zone, flags) == 4);
PIN(offsetof(sc_game_zone, epoch) == 8);
PIN(offsetof(sc_game_zone, zone_id) == 16);
PIN(offsetof(sc_game_zone, parent_id) == 24);
PIN(offsetof(sc_game_zone, root_id) == 32);
PIN(offsetof(sc_game_zone, system_id) == 40);
PIN(offsetof(sc_game_zone, name) == 48);

#define HAS(t, m) ((t) && (t)->size >= offsetof(sc_game_spatial_v1, m) + sizeof((t)->m) && (t)->m)
int sco_game_spatial_route(const sc_game_spatial_v1* table, unsigned method);
int sco_game_spatial_route(const sc_game_spatial_v1* table, unsigned method) {
    double pos[3] = {0}, rot[4] = {0,0,0,1}, out[3], outrot[4];
    sc_game_zone record = {0, 0, 0, 0, 0, 0, 0, {0}};
    uint32_t count = 0, more = 0; uint64_t epoch = 0;
    record.size = sizeof(record);
    switch (method) {
    case 0: return HAS(table, pose_to_zone) ? table->pose_to_zone(20, 30, pos, rot, out, outrot) : 0;
    case 1: return HAS(table, zone_record) ? table->zone_record(20, &record) : 0;
    case 2: return HAS(table, find_zone) ? table->find_zone("synthetic-room", &record) : 0;
    case 3: return HAS(table, list_zones) ? table->list_zones(0, 0, NULL, sizeof(record), 0, &count, &more, &epoch) : 0;
    default: return 0;
    }
}
#ifdef __cplusplus
}
#endif
