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

// The write gate inside AdjustStatValue (actor.stat_authority checks every one of these bytes).
constexpr size_t kStatusEntity       = 0x1F8;   // CSCActorStatus -> owner (its +8 is the entity, masked)
constexpr size_t kEntityHasAuthority = 0x7A8;   // entity vtable slot: bool HasAuthority()
constexpr size_t kRecordDef          = 0x90;    // stat record -> its definition
constexpr size_t kDefLocalChange     = 0x2A;    // definition byte: nonzero = applied without authority

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
    { SC_ACTOR_STAT_BLOOD_DRUG_LEVEL, 0x02, "BloodDrugLevel" },
    { SC_ACTOR_STAT_OVERDOSE_LEVEL,   0x03, "OverdoseLevel" },
    { SC_ACTOR_STAT_PRESSURE,         0x08, "Pressure" },
    { SC_ACTOR_STAT_DOWNED_DAMAGE,    0x0A, "DownedDamage" },
    { SC_ACTOR_STAT_HEALTH_POOL,      0x0B, "HealthPool" },
    { SC_ACTOR_STAT_HEALTH_HEAD,      0x0C, "HealthHead" },
    { SC_ACTOR_STAT_HEALTH_TORSO,     0x0D, "HealthTorso" },
    { SC_ACTOR_STAT_HEALTH_LEFT_ARM,  0x0E, "HealthLeftArm" },
    { SC_ACTOR_STAT_HEALTH_RIGHT_ARM, 0x0F, "HealthRightArm" },
    { SC_ACTOR_STAT_HEALTH_LEFT_LEG,  0x10, "HealthLeftLeg" },
    { SC_ACTOR_STAT_HEALTH_RIGHT_LEG, 0x11, "HealthRightLeg" },
    { SC_ACTOR_STAT_WEAR_HEAD,        0x12, "WearHead" },
    { SC_ACTOR_STAT_WEAR_TORSO,       0x13, "WearTorso" },
    { SC_ACTOR_STAT_WEAR_LEFT_ARM,    0x14, "WearLeftArm" },
    { SC_ACTOR_STAT_WEAR_RIGHT_ARM,   0x15, "WearRightArm" },
    { SC_ACTOR_STAT_WEAR_LEFT_LEG,    0x16, "WearLeftLeg" },
    { SC_ACTOR_STAT_WEAR_RIGHT_LEG,   0x17, "WearRightLeg" },
    { SC_ACTOR_STAT_CARBON_DIOXIDE,   0x1A, "GasSaturationCO2" },
    { SC_ACTOR_STAT_CARBON_MONOXIDE,  0x1B, "GasSaturationCO" },
    { SC_ACTOR_STAT_HYGIENE,          0x1C, "Hygiene" },
};

// The game index for a 1.0 stat; -1 for anything else.
constexpr int IndexOf(uint32_t stat) {
    for (const Entry& e : kEntries)
        if (e.stat && e.stat == stat) return e.index;
    return -1;
}

constexpr bool Writable1_0(uint32_t stat) { return stat > 0 && stat <= SC_ACTOR_STAT_LAST_1_0; }
static_assert(std::size(kEntries) == SC_ACTOR_STAT_LAST_1_1, "one entry per named read stat");
static_assert(IndexOf(SC_ACTOR_STAT_NONE) == -1 && IndexOf(SC_ACTOR_STAT_LAST_1_1 + 1) == -1, "only supported read stats map");

}  // namespace sco::game::actor_stats
