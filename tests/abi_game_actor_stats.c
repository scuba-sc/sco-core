/* Independently compiled C11/C++20 game.actor_stats layout and tail refusal fixture. */
#include <sc_actor_stats.h>
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
PIN(sizeof(sc_actor_stats_v1) == 16, "table size");
PIN(offsetof(sc_actor_stats_v1, size) == 0, "size offset");
PIN(offsetof(sc_actor_stats_v1, read) == 8, "read offset");
PIN(sizeof(sc_actor_stat) == 4, "stat enum is 4 bytes (FORCE32), even with -fshort-enums");
PIN(SC_ACTOR_STAT_HUNGER == 1 && SC_ACTOR_STAT_THIRST == 2 && SC_ACTOR_STAT_BODY_TEMPERATURE == 3, "stat values");
PIN(SC_ACTOR_STAT_SUIT_TEMPERATURE == 4 && SC_ACTOR_STAT_STUN == 5 && SC_ACTOR_STAT_DISTORTION == 6, "stat values");
PIN(SC_ACTOR_STAT_OXYGEN == 7 && SC_ACTOR_STAT_BODY_RADIATION == 8 && SC_ACTOR_STAT_SUIT_RADIATION == 9, "stat values");
PIN(SC_ACTOR_STAT_LAST_1_0 == 9, "1.0 stat range");
PIN(SC_ACTOR_STAT_LAST_1_1 == 29, "1.1 read-only stat range");
PIN(SC_ACTOR_STAT_BLOOD_DRUG_LEVEL == 10 && SC_ACTOR_STAT_HYGIENE == 29, "1.1 appended ids");
PIN(SC_ACTOR_STATS_VERSION_1_1 == 0x00010001u, "1.1 version");
PIN(SC_ACTOR_STATS_VERSION_1_0 == 0x00010000u, "version");
static sco_result read_ok(uint64_t id, uint32_t stat, float* out) {
    (void)id;
    *out = (float)stat;
    return SCO_OK;
}
int main(void) {
    sc_actor_stats_v1 full = {sizeof(full), 0, read_ok};
    float v = 0;
    assert(sc_actor_stats_read(&full, 1, SC_ACTOR_STAT_THIRST, &v) == SCO_OK && v == 2.0f);
    sc_actor_stats_v1 truncated = {offsetof(sc_actor_stats_v1, read), 0, read_ok};
    assert(sc_actor_stats_read(&truncated, 1, SC_ACTOR_STAT_HUNGER, &v) == SCO_UNAVAILABLE);
    assert(sc_actor_stats_read(NULL, 1, SC_ACTOR_STAT_HUNGER, &v) == SCO_UNAVAILABLE);
    sc_actor_stats_v1 nullfn = {sizeof(nullfn), 0, NULL};
    assert(sc_actor_stats_read(&nullfn, 1, SC_ACTOR_STAT_HUNGER, &v) == SCO_UNAVAILABLE);
    return 0;
}
