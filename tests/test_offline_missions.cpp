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
    int reads = 0;
    static Admission Admit(void* p) { return static_cast<Fake*>(p)->allow; }
    static sco_result Offer(void* p, uint32_t i, sc_mission_offer* out) {
        ++static_cast<Fake*>(p)->reads;
        if (i) return SCO_NOT_FOUND;
        *out = {sizeof(*out), 0, 101, 202, 2, 2, 50, 3};
        return SCO_OK;
    }
    static sco_result Status(void* p, uint64_t id, sc_mission_status* out) {
        ++static_cast<Fake*>(p)->reads;
        if (id != 303) return SCO_NOT_FOUND;
        *out = {sizeof(*out), SC_MISSION_ACTIVE, 303, 101, 0, 0, 2, 2, 7};
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
    Backend backend{&f, Fake::Admit, Fake::Offer, Fake::Status};
    CHECK(Start(backend) == Result::Ok);
    const sc_missions_v1* svc = nullptr;
    CHECK(api->query_service(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION,
                            reinterpret_cast<const void**>(&svc)) == SCO_OK);
    CHECK(svc && svc->size >= offsetof(sc_missions_v1, status) + sizeof(svc->status));
    sc_mission_offer offer{}; offer.size = sizeof(offer);
    CHECK(svc->offer(self, 0, &offer) == SCO_OK && offer.offer_id == 101);
    sc_mission_status status{}; status.size = sizeof(status);
    CHECK(svc->status(self, 303, &status) == SCO_OK && status.session == 7);
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
    CHECK(Release(self) == Result::Ok);
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
