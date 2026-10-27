#include "game/services/mission_events.h"
#include "game/services/offline_missions.h"
#include "sco/host.h"
#include "sco/runtime.h"
#include <array>
#include <cstdio>
#include <vector>

using sco::game::services::MissionEvents;
static int failures = 0, passed = 0;
#define CHECK(x) do { if (x) ++passed; else { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)
static std::vector<sc_game_mission_changed> seen;
static void Receive(const char*, const void* payload, void*) {
    seen.push_back(*static_cast<const sc_game_mission_changed*>(payload));
}
static void Send(const sc_game_mission_changed& event) {
    CHECK(sco::Dispatch(SC_GAME_EVENT_MISSION_CHANGED, &event) == sco::Result::Ok);
}
static sc_mission_status Status(uint64_t id, uint64_t session, uint32_t state) {
    return {sizeof(sc_mission_status), state, id, 1, 0, 0, 1, 1, session};
}
struct Backend {
    uint64_t session = 20;
    uint32_t state = SC_MISSION_ACTIVE;
    int accepts = 0, abandons = 0, statuses = 0;
    static sco::game::offline::Admission Admit(void* p) {
        return {static_cast<Backend*>(p)->session, true, true, false};
    }
    static sco_result Offer(void*, uint32_t i, sc_mission_offer* out) {
        if (i) return SCO_NOT_FOUND;
        *out = {sizeof(*out), 0, 1, 2, 1, 1, 0, 3}; return SCO_OK;
    }
    static sco_result Accept(void* p, const sc_mission_offer*, uint64_t* out) {
        ++static_cast<Backend*>(p)->accepts; *out = 303; return SCO_OK;
    }
    static sco_result Status(void* p, uint64_t id, sc_mission_status* out) {
        auto& b = *static_cast<Backend*>(p); ++b.statuses;
        *out = ::Status(id, b.session, b.state); return SCO_OK;
    }
    static sco_result Abandon(void* p, uint64_t) {
        ++static_cast<Backend*>(p)->abandons; return SCO_OK;
    }
};
static void IntegratedPoll() {
    namespace offline = sco::game::offline;
    Backend b;
    offline::Backend backend{};
    backend.ctx = &b; backend.admission = Backend::Admit; backend.offer = Backend::Offer;
    backend.status = Backend::Status; backend.accept = Backend::Accept; backend.abandon = Backend::Abandon;
    const auto* api = sco::host::BuildApi({"synthetic-mission-events"});
    auto* owner = sco::host::NewPlugin("mission_event_fixture"); CHECK(owner);
    CHECK(offline::Start(backend) == sco::Result::Ok);
    const sc_missions_v1* svc = nullptr;
    CHECK(api->query_service(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION,
                             reinterpret_cast<const void**>(&svc)) == SCO_OK);
    sc_mission_offer offer{}; offer.size = sizeof(offer);
    CHECK(svc->offer(owner, 0, &offer) == SCO_OK);
    uint64_t id = 0;
    CHECK(svc->accept(owner, &offer, 1, &id) == SCO_OK && id == 303);
    MissionEvents cache; seen.clear();
    sc_mission_status snapshots[SC_MISSIONS_MAX_ACTIVE]{};
    auto poll = [&] { cache.Observe(snapshots, offline::PollStatuses(snapshots, SC_MISSIONS_MAX_ACTIVE), Send); };
    poll(); CHECK(seen.size() == 1 && seen.back().session == 20);
    b.state = SC_MISSION_COMPLETED; poll();
    CHECK(seen.size() == 2 && seen.back().old_state == SC_MISSION_ACTIVE && seen.back().new_state == SC_MISSION_COMPLETED);
    b.session = 21; b.state = SC_MISSION_ACTIVE;
    const int oldStatuses = b.statuses; poll();
    CHECK(seen.size() == 2 && b.statuses == oldStatuses);
    CHECK(svc->accept(owner, &offer, 1, &id) == SCO_OK && b.accepts == 2 && id == 303);
    poll(); CHECK(seen.size() == 3 && seen.back().session == 21 && seen.back().old_state == SC_MISSION_ACTIVE);
    CHECK(sco::Release(owner) == sco::Result::Ok && b.abandons == 1);
    const int releasedStatuses = b.statuses; poll();
    CHECK(seen.size() == 3 && b.statuses == releasedStatuses);
    CHECK(offline::Stop() == sco::Result::Ok);
}
int main() {
    sco::SetGameThread();
    CHECK(sco::Subscribe(sco::host::HostOwner(), SC_GAME_EVENT_MISSION_CHANGED, Receive, nullptr) == sco::Result::Ok);
    MissionEvents cache;
    auto current = Status(303, 7, SC_MISSION_ACTIVE);
    cache.Observe(&current, 1, Send);
    CHECK(seen.size() == 1 && seen.back().session == 7 && seen.back().old_state == SC_MISSION_ACTIVE);
    cache.Observe(&current, 1, Send);
    CHECK(seen.size() == 1);
    current.state = SC_MISSION_COMPLETED;
    cache.Observe(&current, 1, Send);
    CHECK(seen.size() == 2 && seen.back().old_state == SC_MISSION_ACTIVE && seen.back().new_state == SC_MISSION_COMPLETED);
    cache.Observe(nullptr, 0, Send); // no observation does not establish an empty watch list
    current = Status(303, 8, SC_MISSION_COMPLETED);
    cache.Observe(&current, 1, Send); // reused ID and identical state in a new session
    CHECK(seen.size() == 3 && seen.back().session == 8 && seen.back().old_state == SC_MISSION_COMPLETED);
    current = Status(303, 9, SC_MISSION_ACTIVE);
    cache.Observe(&current, 1, Send); // new session must never inherit prior old_state
    CHECK(seen.size() == 4 && seen.back().session == 9 && seen.back().old_state == SC_MISSION_ACTIVE);
    current.state = SC_MISSION_ABANDONED;
    cache.Observe(&current, 1, Send);
    CHECK(seen.size() == 5 && seen.back().old_state == SC_MISSION_ACTIVE && seen.back().new_state == SC_MISSION_ABANDONED);
    cache.Observe(&current, 1, Send);
    CHECK(seen.size() == 5); // terminal transition once
    auto other = Status(404, 9, SC_MISSION_ACTIVE);
    cache.Observe(&other, 1, Send); // nonempty observation prunes the old ID
    CHECK(seen.size() == 6 && seen.back().mission_id == 404);
    cache.Observe(&current, 1, Send);
    CHECK(seen.size() == 7 && seen.back().old_state == SC_MISSION_ABANDONED);
    cache.Reset();
    cache.Observe(&current, 1, Send);
    CHECK(seen.size() == 8 && seen.back().old_state == SC_MISSION_ABANDONED);

    // A full watch list can be replaced by reused IDs in a new session, then stays stable.
    cache.Reset(); seen.clear();
    std::array<sc_mission_status, SC_MISSIONS_MAX_ACTIVE> full{};
    for (size_t i = 0; i < full.size(); ++i) full[i] = Status(i + 1, 10, SC_MISSION_ACTIVE);
    cache.Observe(full.data(), static_cast<uint32_t>(full.size()), Send);
    for (auto& item : full) item.session = 11;
    cache.Observe(full.data(), static_cast<uint32_t>(full.size()), Send);
    CHECK(seen.size() == 2 * full.size() && seen.back().session == 11);
    cache.Observe(full.data(), static_cast<uint32_t>(full.size()), Send);
    CHECK(seen.size() == 2 * full.size());
    IntegratedPoll();
    CHECK(sco::Unsubscribe(sco::host::HostOwner(), SC_GAME_EVENT_MISSION_CHANGED, Receive) == sco::Result::Ok);
    std::printf("mission events: %d passed, %d failed\n", passed, failures);
    return failures ? 1 : 0;
}
