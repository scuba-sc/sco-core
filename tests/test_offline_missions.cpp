#include "../src/game/services/offline_missions.h"
#include "sco/host.h"
#include "scosdk/game/missions.hpp"
#include <cstring>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
using namespace sco;
using namespace sco::game::offline;
struct Fake {
    Admission allow{7, true, true, false};
    int reads = 0, accepts = 0, objectives = 0, advances = 0, abandons = 0, ingests = 0;
    uint32_t phase = 0, objective = 0, state = SC_MISSION_ACTIVE;
    uint64_t lastMission = 303;
    static Admission Admit(void* p) { return static_cast<Fake*>(p)->allow; }
    static sco_result Offer(void* p, uint32_t i, sc_mission_offer* out) {
        ++static_cast<Fake*>(p)->reads;
        if (i) return SCO_NOT_FOUND;
        *out = {sizeof(*out), 0, 101, 202, 2, 2, 50, 3};
        return SCO_OK;
    }
    static sco_result Accept(void* p, const sc_mission_offer*, uint64_t* out) {
        auto& f = *static_cast<Fake*>(p); ++f.accepts; *out = f.lastMission = 302 + f.accepts; return SCO_OK;
    }
    static sco_result Objective(void* p, uint64_t, uint32_t, uint32_t) {
        auto& f = *static_cast<Fake*>(p); ++f.objectives; ++f.objective; return SCO_OK;
    }
    static sco_result Advance(void* p, uint64_t, uint32_t) {
        auto& f = *static_cast<Fake*>(p); ++f.advances;
        if (f.phase == 1) f.state = SC_MISSION_COMPLETED;
        else { ++f.phase; f.objective = 0; }
        return SCO_OK;
    }
    static sco_result Abandon(void* p, uint64_t) {
        ++static_cast<Fake*>(p)->abandons; return SCO_OK;
    }
    static sco_result Ingest(void* p, const sc_mission_script*) {
        ++static_cast<Fake*>(p)->ingests; return SCO_OK;
    }
    static sco_result Status(void* p, uint64_t id, sc_mission_status* out) {
        ++static_cast<Fake*>(p)->reads;
        if (id != static_cast<Fake*>(p)->lastMission) return SCO_NOT_FOUND;
        auto& f = *static_cast<Fake*>(p);
        *out = {sizeof(*out), f.state, f.lastMission, 101, f.phase, f.objective, 2, 2, f.allow.session};
        return SCO_OK;
    }
};
static const void* exposed;
static sco_result Query(const char*, uint32_t, const void** out) { *out = exposed; return exposed ? SCO_OK : SCO_NOT_FOUND; }
void WrapperGuards(sco_plugin* self) {
    sco_api api{}; api.size = sizeof(api); api.query_service = Query;
    sco::sdk::game::Missions wrap;
    sc_mission_offer offer{}; offer.size = sizeof(offer);
    sc_mission_status status{}; status.size = sizeof(status);
    for (uint32_t n = 8; n < sizeof(sc_missions_v1); ++n) {
        auto bytes = std::make_unique<unsigned char[]>(n);
        std::memset(bytes.get(), 0, n); std::memcpy(bytes.get(), &n, sizeof(n));
        exposed = bytes.get(); CHECK(wrap.Open(&api, self) == SCO_OK);
        CHECK(wrap.Offer(0, offer) == SCO_UNAVAILABLE);
        CHECK(wrap.Status(303, status) == SCO_UNAVAILABLE);
        uint64_t id = 0; CHECK(wrap.Accept(offer, 42, id) == SCO_UNAVAILABLE);
        CHECK(wrap.Objective(303, 0, 0) == SCO_UNAVAILABLE);
        CHECK(wrap.Advance(303, 0) == SCO_UNAVAILABLE);
        CHECK(wrap.Abandon(303) == SCO_UNAVAILABLE);
        sc_mission_script script{}; CHECK(wrap.Ingest(script) == SCO_UNAVAILABLE);
    }
    sc_missions_v1 empty{}; empty.size = sizeof(empty); exposed = &empty;
    CHECK(wrap.Open(&api, self) == SCO_OK);
    CHECK(wrap.Offer(0, offer) == SCO_UNAVAILABLE && wrap.Status(303, status) == SCO_UNAVAILABLE);
    exposed = nullptr; CHECK(wrap.Open(&api, self) == SCO_NOT_FOUND);
    CHECK(wrap.Offer(0, offer) == SCO_UNAVAILABLE);
}
int main() {
    SetGameThread();
    const auto* api = host::BuildApi({"synthetic-offline"});
    auto* self = host::NewPlugin("mission_fixture"); CHECK(self);
    WrapperGuards(self);
    Fake f;
    Backend backend{&f, Fake::Admit, Fake::Offer, Fake::Status, Fake::Accept, Fake::Objective, Fake::Advance, Fake::Abandon, Fake::Ingest};
    CHECK(Start(backend) == Result::Ok);
    const sc_missions_v1* svc = nullptr;
    CHECK(api->query_service(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION,
                            reinterpret_cast<const void**>(&svc)) == SCO_OK);
    CHECK(svc && svc->size >= offsetof(sc_missions_v1, status) + sizeof(svc->status));
    sc_mission_offer offer{}; offer.size = sizeof(offer);
    CHECK(svc->offer(self, 0, &offer) == SCO_OK && offer.offer_id == 101);
    sc_mission_status status{}; status.size = sizeof(status);
    CHECK(svc->status(self, 303, &status) == SCO_OK && status.session == 7);
    uint64_t mission = 0;
    CHECK(svc->accept(self, &offer, 42, &mission) == SCO_OK && mission == 303 && f.accepts == 1);
    CHECK(svc->accept(self, &offer, 42, &mission) == SCO_OK && f.accepts == 1);
    auto changed = offer; changed.broker_id++;
    CHECK(svc->accept(self, &changed, 42, &mission) == SCO_BAD_ARG && f.accepts == 1);
    changed = offer; changed.generation++;
    CHECK(svc->accept(self, &changed, 43, &mission) == SCO_NOT_FOUND && f.accepts == 1);
    CHECK(svc->accept(self, &offer, 0, &mission) == SCO_BAD_ARG && f.accepts == 1);
    auto* foreign = host::NewPlugin("mission_foreign"); CHECK(foreign);
    CHECK(svc->objective(foreign, mission, 0, 0) == SCO_NOT_FOUND && f.objectives == 0);
    CHECK(svc->advance(self, mission, 0) == SCO_FAILED && f.advances == 0);
    CHECK(svc->objective(self, mission, 0, 0) == SCO_OK && f.objectives == 1);
    CHECK(svc->objective(self, mission, 0, 0) == SCO_OK && f.objectives == 1);
    CHECK(svc->objective(self, mission, 0, 2) == SCO_BAD_ARG && f.objectives == 1);
    CHECK(svc->objective(self, mission, 0, 1) == SCO_OK && f.objectives == 2);
    CHECK(svc->advance(self, mission, 0) == SCO_OK && f.advances == 1);
    CHECK(svc->advance(self, mission, 0) == SCO_OK && f.advances == 1);
    CHECK(svc->objective(self, mission, 1, 0) == SCO_OK);
    CHECK(svc->objective(self, mission, 1, 1) == SCO_OK);
    CHECK(svc->advance(self, mission, 1) == SCO_OK && f.state == SC_MISSION_COMPLETED);
    CHECK(svc->advance(self, mission, 1) == SCO_OK && f.advances == 2);
    CHECK(svc->abandon(self, mission) == SCO_FAILED && f.abandons == 0); // completed
    const char text[] = "sco-mission 1\nphases 2\nobjectives 2\n";
    sc_mission_script script{}; script.size = sizeof(script);
    std::strcpy(script.relative_path, "missions/synthetic.scom");
    script.bytes = text; script.byte_count = sizeof(text) - 1;
    CHECK(svc->ingest(self, &script) == SCO_OK && f.ingests == 1);
    CHECK(svc->ingest(self, &script) == SCO_OK && f.ingests == 1);
    std::strcpy(script.relative_path, "missions/../escape.scom");
    CHECK(svc->ingest(self, &script) == SCO_BAD_ARG && f.ingests == 1);
    std::strcpy(script.relative_path, "missions/synthetic.scom");
    script.bytes = "sco-mission 1\nphases 2\nobjectives 2\nnetwork evil\n";
    script.byte_count = static_cast<uint32_t>(std::strlen(script.bytes));
    CHECK(svc->ingest(self, &script) == SCO_BAD_ARG && f.ingests == 1);
    script.bytes = "sco-mission 1\nphases 3\nobjectives 2\n";
    script.byte_count = static_cast<uint32_t>(std::strlen(script.bytes));
    CHECK(svc->ingest(self, &script) == SCO_FAILED && f.ingests == 1); // restart to edit
    script.bytes = text; script.byte_count = sizeof(text) - 1;
    std::strcpy(script.relative_path, "missions/nul.scom");
    CHECK(svc->ingest(self, &script) == SCO_BAD_ARG);
    for (unsigned i = 1; i < SC_MISSIONS_MAX_SCRIPTS; ++i) {
        std::snprintf(script.relative_path, sizeof(script.relative_path), "missions/synthetic_%u.scom", i);
        CHECK(svc->ingest(self, &script) == SCO_OK);
    }
    std::strcpy(script.relative_path, "missions/overflow.scom");
    CHECK(svc->ingest(self, &script) == SCO_TOO_MANY && f.ingests == SC_MISSIONS_MAX_SCRIPTS);
    CHECK(svc->offer(self, 1, &offer) == SCO_NOT_FOUND);
    int before = f.reads;
    f.allow.offline = false;
    CHECK(svc->offer(self, 0, &offer) == SCO_UNAVAILABLE && f.reads == before);
    f.allow.offline = true; f.allow.eac_absent = false;
    CHECK(svc->status(self, 303, &status) == SCO_UNAVAILABLE && f.reads == before);
    f.allow.eac_absent = true; f.allow.shutting_down = true;
    CHECK(svc->offer(self, 0, &offer) == SCO_UNAVAILABLE && f.reads == before);
    f.allow.shutting_down = false;
    std::thread worker([&] { CHECK(svc->offer(self, 0, &offer) == SCO_WRONG_THREAD); }); worker.join();
    CHECK(f.reads == before);
    offer.size = 4; CHECK(svc->offer(self, 0, &offer) == SCO_BAD_ARG); offer.size = sizeof(offer);
    CHECK(svc->offer(self, SC_MISSIONS_MAX_OFFERS, &offer) == SCO_BAD_ARG);
    CHECK(svc->offer(nullptr, 0, &offer) == SCO_BAD_ARG);
    f.allow.session = 8; f.phase = f.objective = 0; f.state = SC_MISSION_ACTIVE;
    CHECK(svc->objective(self, mission, 0, 0) == SCO_NOT_FOUND); // stale session
    CHECK(svc->accept(self, &offer, 99, &mission) == SCO_OK);
    CHECK(svc->abandon(foreign, mission) == SCO_NOT_FOUND);
    CHECK(svc->abandon(self, mission) == SCO_OK && f.abandons == 1);
    CHECK(svc->abandon(self, mission) == SCO_OK && f.abandons == 1);
    f.allow.session = 9;
    CHECK(svc->accept(self, &offer, 100, &mission) == SCO_OK);
    CHECK(Release(self) == Result::Ok && f.abandons == 2);
    CHECK(svc->status(self, 303, &status) == SCO_BAD_ARG);
    CHECK(Stop() == Result::Ok);
    CHECK(api->query_service(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION,
                            reinterpret_cast<const void**>(&svc)) == SCO_NOT_FOUND);
    CHECK(Start() == Result::Ok);
    self = host::NewPlugin("mission_absent"); CHECK(self);
    CHECK(api->query_service(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION,
                            reinterpret_cast<const void**>(&svc)) == SCO_OK);
    CHECK(svc->offer(self, 0, &offer) == SCO_UNAVAILABLE);
    CHECK(Stop() == Result::Ok);
    std::puts("offline missions read/status: PASS (synthetic backend, no game)");
}
