/* Independently compiled C11/C++20 game.actor_stats_write layout and tail refusal fixture. */
#include <sc_actor_stats_write.h>
#include <stddef.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sc_actor_stats_write_v1) == 24, "table size");
PIN(offsetof(sc_actor_stats_write_v1, size) == 0, "size offset");
PIN(offsetof(sc_actor_stats_write_v1, adjust) == 8, "adjust offset");
PIN(offsetof(sc_actor_stats_write_v1, set) == 16, "set offset");
PIN(SC_ACTOR_STATS_WRITE_VERSION_1_0 == 0x00010000u, "version");
static sco_result adjust_ok(sco_plugin* p, uint64_t id, uint32_t stat, float delta) {
    (void)p; (void)id; (void)stat;
    return delta == 1.0f ? SCO_OK : SCO_BAD_ARG;
}
static sco_result set_ok(sco_plugin* p, uint64_t id, uint32_t stat, float value) {
    (void)p; (void)id; (void)value;
    return stat == SC_ACTOR_STAT_HUNGER ? SCO_OK : SCO_BAD_ARG;
}
int main(void) {
    sc_actor_stats_write_v1 full = {sizeof(full), 0, adjust_ok, set_ok};
    assert(sc_actor_stats_adjust(&full, NULL, 1, SC_ACTOR_STAT_HUNGER, 1.0f) == SCO_OK);
    assert(sc_actor_stats_set(&full, NULL, 1, SC_ACTOR_STAT_HUNGER, 5.0f) == SCO_OK);
    sc_actor_stats_write_v1 adjust_only = {offsetof(sc_actor_stats_write_v1, set), 0, adjust_ok, set_ok};
    assert(sc_actor_stats_adjust(&adjust_only, NULL, 1, SC_ACTOR_STAT_HUNGER, 1.0f) == SCO_OK);
    assert(sc_actor_stats_set(&adjust_only, NULL, 1, SC_ACTOR_STAT_HUNGER, 5.0f) == SCO_UNAVAILABLE);
    sc_actor_stats_write_v1 truncated = {offsetof(sc_actor_stats_write_v1, adjust), 0, adjust_ok, set_ok};
    assert(sc_actor_stats_adjust(&truncated, NULL, 1, SC_ACTOR_STAT_HUNGER, 1.0f) == SCO_UNAVAILABLE);
    assert(sc_actor_stats_adjust(NULL, NULL, 1, SC_ACTOR_STAT_HUNGER, 1.0f) == SCO_UNAVAILABLE);
    sc_actor_stats_write_v1 nullfn = {sizeof(nullfn), 0, NULL, NULL};
    assert(sc_actor_stats_set(&nullfn, NULL, 1, SC_ACTOR_STAT_HUNGER, 5.0f) == SCO_UNAVAILABLE);
    return 0;
}
