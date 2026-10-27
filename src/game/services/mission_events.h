#pragma once
#include <sc_game_events.h>
#include <sc_missions.h>
#include <cstddef>
#include <cstdint>

namespace sco::game::services {
// Game-thread cache decision shared by the Windows tick and portable tests.
class MissionEvents {
    struct Previous { uint64_t id; uint32_t state, phase, objective; uint64_t session; };
    Previous previous_[SC_MISSIONS_MAX_ACTIVE]{};
    size_t count_ = 0;
public:
    void Reset() { count_ = 0; }

    template<class Publish>
    void Observe(const sc_mission_status* current, uint32_t n, Publish publish) {
        if (!n) return;
        Previous next[SC_MISSIONS_MAX_ACTIVE]{};
        size_t nextCount = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const auto& c = current[i];
            Previous* prev = nullptr;
            for (size_t j = 0; j < count_; ++j)
                if (previous_[j].id == c.mission_id && previous_[j].session == c.session) {
                    prev = &previous_[j]; break;
                }
            const bool changed = !prev || prev->state != c.state || prev->phase != c.phase ||
                                 prev->objective != c.objective;
            if (changed) {
                sc_game_mission_changed event = {sizeof(event), 0, c.mission_id,
                    prev ? prev->state : c.state, c.state, c.phase, c.objective, c.session};
                publish(event);
            }
            if (prev) { prev->state = c.state; prev->phase = c.phase; prev->objective = c.objective; prev->session = c.session; }
            else if (nextCount < SC_MISSIONS_MAX_ACTIVE)
                next[nextCount++] = {c.mission_id, c.state, c.phase, c.objective, c.session};
        }
        size_t kept = 0;
        for (size_t j = 0; j < count_; ++j) {
            bool alive = false;
            for (uint32_t i = 0; i < n; ++i)
                if (current[i].mission_id == previous_[j].id && current[i].session == previous_[j].session) {
                    alive = true; break;
                }
            if (alive) previous_[kept++] = previous_[j];
        }
        for (size_t j = 0; j < nextCount && kept < SC_MISSIONS_MAX_ACTIVE; ++j, ++kept)
            previous_[kept] = next[j];
        count_ = kept;
    }
};
}
