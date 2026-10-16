#pragma once
// game.actor_stats inside the game pack: which entry of the game's actor stat table each
// sc_actor_stat names (F-4.10.197.2878-006). Private to src/game/.
//
// The game keeps an actor's stats as an array of record pointers in its CSCActorStatus, indexed by
// a stat-type enum; GetStat(status, index) reads the current value (actor.get_stat). The same
// enum has a name function (index -> name) in the game. The actor.stat_names row checks, on every
// game build, that each index below still maps to the name below, plus HealthPool (the index
// game.actors' health reads) as a cross-check. A reordered enum fails the row, so the capability
// goes off instead of reading the wrong stat.
#include <sc_actor_stats.h>
#include <cstddef>
#include <iterator>

namespace sco::game::actor_stats {

struct Entry {
    uint32_t    stat;    // sc_actor_stat, 0 for a cross-check-only entry
    int         index;   // the game's stat index
    const char* name;    // what the game's name function answers for index
};

constexpr int kNameCount = 0x1D;   // the name function's bound: cmp edx,1Ch; ja <undefined>

constexpr Entry kEntries[] = {
    { SC_ACTOR_STAT_HUNGER,           0x00, "Hunger" },
    { SC_ACTOR_STAT_THIRST,           0x01, "Thirst" },
    { SC_ACTOR_STAT_BODY_TEMPERATURE, 0x04, "BodyTemperature" },
    { SC_ACTOR_STAT_SUIT_TEMPERATURE, 0x05, "SuitTemperature" },
    { SC_ACTOR_STAT_STUN,             0x06, "Stun" },
    { SC_ACTOR_STAT_DISTORTION,       0x07, "Distortion" },
    { SC_ACTOR_STAT_OXYGEN,           0x09, "GasSaturationO2" },
    { SC_ACTOR_STAT_BODY_RADIATION,   0x18, "BodyRadiation" },
    { SC_ACTOR_STAT_SUIT_RADIATION,   0x19, "SuitRadiation" },
    { 0,                              0x0B, "HealthPool" },   // cross-check: game.actors health's index
};

// The game index for a 1.0 stat; -1 for anything else.
constexpr int IndexOf(uint32_t stat) {
    for (const Entry& e : kEntries)
        if (e.stat && e.stat == stat) return e.index;
    return -1;
}

static_assert(std::size(kEntries) == SC_ACTOR_STAT_LAST_1_0 + 1, "one entry per 1.0 stat plus the cross-check");
static_assert(IndexOf(SC_ACTOR_STAT_NONE) == -1 && IndexOf(SC_ACTOR_STAT_LAST_1_0 + 1) == -1, "only 1.0 stats map");

}  // namespace sco::game::actor_stats
