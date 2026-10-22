// SPDX-License-Identifier: GPL-3.0-only
#include "../src/game/actor_stats_map.h"
#include <cstdio>
#include <cstring>
namespace M = sco::game::actor_stats;
int main() {
    const char* expected[] = {"Hunger", "Thirst", "BloodDrugLevel", "OverdoseLevel",
        "BodyTemperature", "SuitTemperature", "Stun", "Distortion", "Pressure",
        "GasSaturationO2", "DownedDamage", "HealthPool", "HealthHead", "HealthTorso",
        "HealthLeftArm", "HealthRightArm", "HealthLeftLeg", "HealthRightLeg",
        "WearHead", "WearTorso", "WearLeftArm", "WearRightArm", "WearLeftLeg",
        "WearRightLeg", "BodyRadiation", "SuitRadiation", "GasSaturationCO2",
        "GasSaturationCO", "Hygiene"};
    unsigned failed = 0;
    bool seen[29]{};
    for (uint32_t stat = 1; stat <= SC_ACTOR_STAT_LAST_1_1; ++stat) {
        const int index = M::IndexOf(stat);
        if (index < 0 || index >= 29 || seen[index]) { ++failed; continue; }
        seen[index] = true;
        for (const auto& entry : M::kEntries)
            if (entry.stat == stat && std::strcmp(entry.name, expected[index])) ++failed;
        if (M::Writable1_0(stat) != (stat <= 9)) ++failed;
    }
    if (M::IndexOf(0) != -1 || M::IndexOf(30) != -1 || M::Writable1_0(0) || M::Writable1_0(30)) ++failed;
    std::printf("actor condition names and write boundary: %u failed\n", failed);
    return failed ? 1 : 0;
}
