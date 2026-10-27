// SPDX-License-Identifier: GPL-3.0-only
// Cross-service consumer policy fixture. All rewards, IDs and storage are synthetic.
#include "../src/game/services/offline_missions.h"
#include "../src/game/services/container_contents.h"
#include "sco/host.h"
#include "sco/caps.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace sco;
namespace M = sco::game::offline;
namespace C = sco::game::services;

struct Fake {
    M::Admission admission{1, true, true, false};
    uint32_t state = SC_MISSION_ACTIVE, objective = 0;
    int64_t balance = 50;
    unsigned accepts = 0, advances = 0, payments = 0, abandons = 0;
    bool uncertain = false;
    int container = 0;
    static M::Admission Admit(void* p) { return static_cast<Fake*>(p)->admission; }
    static sco_result Offer(void*, uint32_t i, sc_mission_offer* out) {
        if (i) return SCO_NOT_FOUND;
        *out = {sizeof(*out), 0, 10, 20, 1, 1, 999, 1}; return SCO_OK;
    }
    static sco_result Status(void* p, uint64_t id, sc_mission_status* out) {
        auto& f = *static_cast<Fake*>(p); if (id != 29 + f.accepts) return SCO_NOT_FOUND;
        *out = {sizeof(*out), f.state, id, 10, 0, f.objective, 1, 1, f.admission.session}; return SCO_OK;
    }
    static sco_result Accept(void* p, const sc_mission_offer*, uint64_t* id) {
        auto& f = *static_cast<Fake*>(p); *id = 29 + ++f.accepts; return SCO_OK;
    }
    static sco_result Objective(void* p, uint64_t, uint32_t, uint32_t) {
        ++static_cast<Fake*>(p)->objective; return SCO_OK;
    }
    static sco_result Advance(void* p, uint64_t, uint32_t) {
        auto& f = *static_cast<Fake*>(p); ++f.advances; f.state = SC_MISSION_COMPLETED; return SCO_OK;
    }
    static sco_result Abandon(void* p, uint64_t) { ++static_cast<Fake*>(p)->abandons; return SCO_OK; }
    static sco_result Balance(void* p, int64_t* out) { *out = static_cast<Fake*>(p)->balance; return SCO_OK; }
    static sco_result Apply(void* p, int64_t expected, int64_t delta, int64_t* out) {
        auto& f = *static_cast<Fake*>(p); ++f.payments;
        if (f.balance != expected) return SCO_FAILED;
        f.balance += delta; *out = f.balance; return f.uncertain ? SCO_FAILED : SCO_OK;
    }
    static sco_result Contents(void* p, uint64_t id, sc_container_snapshot* out) {
        auto& f = *static_cast<Fake*>(p);
        if (f.container == 2) return SCO_NOT_FOUND;
        out->container_id = id; out->generation = f.container == 3 ? 2 : 1;
        out->count = f.container == 1 ? 0 : 1; out->total_seen = out->count;
        out->flags = f.container == 4 ? 0 : SC_CONTAINER_CONTENTS_VOLUME_KNOWN;
        if (out->count) {
            out->items[0] = {sizeof(sc_container_item), 1, 41, 51, f.container == 4 ? 0u : 10u};
            out->used_volume = f.container == 4 ? 0 : 10;
        }
        return SCO_OK;
    }
};

// Deliberately conservative, test-only persistence model; no claim of durable storage integration.
struct Journal {
    enum Stage { Empty, Intent, Paid, Unknown } stage = Empty, committed = Empty;
    uint64_t session = 0, mission = 0, offer = 0, generation = 0;
    sc_economy_request request{};
    sc_economy_receipt receipt{};
    bool fail_intent_commit = false, fail_receipt_commit = false;
    Journal Restore() const {
        Journal copy = *this; copy.stage = committed == Intent ? Unknown : committed; return copy;
    }
    bool CommitIntent(uint64_t current_session, uint64_t current_mission, uint64_t current_offer,
                      uint64_t current_generation, const sc_economy_request& next) {
        if (fail_intent_commit) return false;
        session = current_session; mission = current_mission; offer = current_offer; generation = current_generation;
        request = next; stage = committed = Intent; return true;
    }
    bool CommitReceipt(const sc_economy_receipt& next) {
        if (fail_receipt_commit) return false;
        receipt = next; stage = committed = Paid; return true;
    }
};
#define HAS(t, T, member) ((t) && (t)->size >= offsetof(T, member) + sizeof((t)->member) && (t)->member)
struct Consumer {
    const sco_api* api;
    sco_plugin* owner;
    Journal& journal;
    const sc_missions_v1* missions = nullptr;
    const sc_economy_v1* economy = nullptr;
    const sc_container_contents_v1* contents = nullptr;
    uint64_t owned_mission = 0, owned_offer = 0, owned_generation = 0, owned_session = 0;
    void BindAccepted(uint64_t mission, const sc_mission_offer& offer, uint64_t session) {
        owned_mission = mission; owned_offer = offer.offer_id; owned_generation = offer.generation; owned_session = session;
    }
    bool Open() {
        missions = nullptr; economy = nullptr; contents = nullptr;
        const void* raw = nullptr;
        if (!HAS(api, sco_api, query_service) || !HAS(api, sco_api, has)) return false;
        if (api->query_service(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION, &raw) != SCO_OK) return false;
        missions = static_cast<const sc_missions_v1*>(raw); raw = nullptr;
        if (api->query_service(SC_ECONOMY_SERVICE_NAME, SC_ECONOMY_SERVICE_VERSION, &raw) != SCO_OK) return false;
        economy = static_cast<const sc_economy_v1*>(raw); raw = nullptr;
        if (api->query_service(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &raw) == SCO_OK)
            contents = static_cast<const sc_container_contents_v1*>(raw);
        return HAS(missions, sc_missions_v1, status) && HAS(economy, sc_economy_v1, balance) &&
               HAS(economy, sc_economy_v1, apply) && HAS(economy, sc_economy_v1, receipt) &&
               api->has("game.missions.read") && api->has("game.economy.read") && api->has("game.economy.apply");
    }
    sco_result Reward(uint64_t mission, uint64_t session) {
        if (!Open()) return SCO_UNAVAILABLE;
        // Observation is not ownership: only the locally recorded successful acceptance is eligible.
        if (!owned_mission || mission != owned_mission || session != owned_session ||
            owned_offer != 10 || owned_generation != 1) return SCO_NOT_FOUND;
        sc_mission_status status{}; status.size = sizeof(status);
        auto r = missions->status(owner, mission, &status);
        if (r != SCO_OK) return r;
        if (status.session != session || status.offer_id != owned_offer || status.state != SC_MISSION_COMPLETED) return SCO_FAILED;
        if (journal.stage != Journal::Empty && (journal.session != session || journal.mission != mission ||
            journal.offer != owned_offer || journal.generation != owned_generation)) {
            journal.stage = Journal::Unknown; return SCO_FAILED;
        }
        if (journal.stage == Journal::Paid) return SCO_OK;
        if (journal.stage == Journal::Unknown) return SCO_FAILED;
        if (journal.stage == Journal::Empty) {
            sc_economy_balance balance{}; balance.size = sizeof(balance);
            if ((r = economy->balance(owner, &balance)) != SCO_OK) return r;
            const sc_economy_request request{sizeof(sc_economy_request), 0, 100, balance.local_balance, 20};
            if (!journal.CommitIntent(session, mission, owned_offer, owned_generation, request)) return SCO_FAILED;
        }
        sc_economy_receipt receipt{}; receipt.size = sizeof(receipt);
        r = economy->apply(owner, &journal.request, &receipt);
        if (r != SCO_OK) { journal.stage = Journal::Unknown; return r; }
        if (!journal.CommitReceipt(receipt)) { journal.stage = Journal::Unknown; return SCO_FAILED; }
        return SCO_OK;
    }
    struct ItemObservation { sco_result result; uint64_t generation; uint32_t count, total, flags; bool stale, deliverable; };
    // Always unknown for delivery, but return the actual observation to the consumer.
    ItemObservation InspectItem(uint64_t id, uint32_t capacity, uint64_t prior_generation = 0) {
        if (!Open() || !contents || !api->has("game.inventory.contents.read")) return {SCO_UNAVAILABLE, 0, 0, 0, 0, false, false};
        sc_container_snapshot out{}; out.size = sizeof(out); out.count = capacity;
        const auto result = sc_container_contents_read(contents, owner, id, &out);
        const bool stale = prior_generation && prior_generation != out.generation;
        return {result, out.generation, out.count, out.total_seen, out.flags, stale, false};
    }
};

static const void* stub_table = nullptr;
static const char* stub_name = nullptr;
static const sco_api* real_api = nullptr;
static sco_result StubQuery(const char* name, uint32_t version, const void** out) {
    if (stub_name && std::strcmp(name, stub_name) == 0) {
        *out = stub_table; return stub_table ? SCO_OK : SCO_NOT_FOUND;
    }
    return real_api->query_service(name, version, out);
}

struct Signals {
    unsigned count = 0; const sc_economy_v1* table = nullptr; sco_plugin* owner = nullptr;
    static void Event(const char*, const void* payload, void* ctx) {
        auto& self = *static_cast<Signals*>(ctx); ++self.count;
        const auto copied = *static_cast<const sc_economy_event*>(payload);
        CHECK(copied.receipt.key == 100 && copied.receipt.after == 70);
        sc_economy_receipt out{}; out.size = sizeof(out);
        CHECK(self.table->receipt(self.owner, 100, &out) == SCO_FAILED);
    }
};

int main() {
    SetGameThread(); const auto* api = host::BuildApi({"synthetic reward workflow"});
    auto* self = host::NewPlugin("reward_fixture"); CHECK(self);
    Fake f; M::Backend b{&f, Fake::Admit, Fake::Offer, Fake::Status, Fake::Accept, Fake::Objective,
                          Fake::Advance, Fake::Abandon, nullptr, Fake::Balance, Fake::Apply};
    CHECK(M::Start(b) == Result::Ok); CHECK(C::StartContainerContents({&f, Fake::Contents}) == Result::Ok);
    Journal journal; Consumer consumer{api, self, journal}; CHECK(consumer.Open());
    sco_api stub_api = *api; stub_api.query_service = StubQuery; real_api = api;
    Journal guard_journal; Consumer guarded{&stub_api, self, guard_journal};
    stub_name = SC_MISSIONS_SERVICE_NAME; stub_table = nullptr; CHECK(!guarded.Open());
    alignas(8) unsigned char short_table[8]{}; const uint32_t short_size = sizeof(short_table);
    std::memcpy(short_table, &short_size, sizeof(short_size)); stub_table = short_table;
    CHECK(!guarded.Open());
    sc_missions_v1 null_mission = *consumer.missions; null_mission.status = nullptr; stub_table = &null_mission;
    CHECK(!guarded.Open());
    stub_name = SC_ECONOMY_SERVICE_NAME; stub_table = short_table; CHECK(!guarded.Open());
    sc_economy_v1 null_economy = *consumer.economy; null_economy.apply = nullptr; stub_table = &null_economy;
    CHECK(!guarded.Open()); stub_name = nullptr; stub_table = nullptr;
    CHECK(HAS(consumer.missions, sc_missions_v1, offer) && HAS(consumer.missions, sc_missions_v1, accept));
    CHECK(HAS(consumer.missions, sc_missions_v1, objective) && HAS(consumer.missions, sc_missions_v1, advance));
    sc_mission_offer offer{}; offer.size = sizeof(offer); uint64_t id = 0;
    CHECK(consumer.missions->offer(self, 0, &offer) == SCO_OK);
    CHECK(consumer.missions->accept(self, &offer, 1, &id) == SCO_OK && id == 30);
    CHECK(consumer.Reward(id, 1) == SCO_NOT_FOUND && f.payments == 0);
    auto wrong = offer; wrong.offer_id = 11; consumer.BindAccepted(id, wrong, 1);
    CHECK(consumer.Reward(id, 1) == SCO_NOT_FOUND && f.payments == 0);
    consumer.BindAccepted(id, offer, 1);
    Signals signals{0, consumer.economy, self};
    CHECK(Subscribe(self, SC_ECONOMY_TRANSACTION_EVENT, Signals::Event, &signals) == Result::Ok);
    CHECK(consumer.Reward(id, 1) == SCO_FAILED && f.payments == 0);
    CHECK(consumer.missions->objective(self, id, 0, 0) == SCO_OK);
    CHECK(consumer.missions->advance(self, id, 0) == SCO_OK && f.payments == 0);
    CHECK(consumer.Reward(id, 1) == SCO_OK && f.payments == 1 && f.balance == 70);
    CHECK(journal.receipt.before == 50 && journal.receipt.after == 70 && journal.receipt.delta == 20);
    CHECK(consumer.Reward(id, 1) == SCO_OK && f.payments == 1 && signals.count == 1);
    CHECK(Unsubscribe(self, SC_ECONOMY_TRANSACTION_EVENT, Signals::Event) == Result::Ok);
    f.admission.offline = false; CHECK(consumer.Reward(id, 1) == SCO_UNAVAILABLE && f.payments == 1); f.admission.offline = true;
    Journal paid_restore = journal.Restore(); CHECK(paid_restore.stage == Journal::Paid);
    Journal pre_effect; CHECK(pre_effect.CommitIntent(1, id, offer.offer_id, offer.generation, {sizeof(sc_economy_request), 0, 104, 70, 1}));
    Journal pre_restore = pre_effect.Restore(); CHECK(pre_restore.stage == Journal::Unknown);
    Journal refused; refused.fail_intent_commit = true; Consumer no_commit{api, self, refused}; no_commit.BindAccepted(id, offer, 1);
    CHECK(no_commit.Reward(id, 1) == SCO_FAILED && f.payments == 1 && refused.stage == Journal::Empty);
    sc_economy_receipt receipt{}; receipt.size = sizeof(receipt);
    CHECK(consumer.economy->apply(self, &journal.request, &receipt) == SCO_OK && f.payments == 1);
    auto altered = journal.request; ++altered.delta;
    CHECK(consumer.economy->apply(self, &altered, &receipt) == SCO_BAD_ARG && f.payments == 1);
    // Buffer capacity, total items, volume knowledge and generation are distinct.
    f.container = 0; sc_container_snapshot snapshot{}; snapshot.size = sizeof(snapshot); snapshot.count = 0;
    CHECK(sc_container_contents_read(consumer.contents, self, 42, &snapshot) == SCO_OK);
    CHECK(snapshot.count == 0 && snapshot.total_seen == 1 && (snapshot.flags & SC_CONTAINER_CONTENTS_TRUNCATED));
    f.container = 1; snapshot = {}; snapshot.size = sizeof(snapshot); snapshot.count = 64;
    CHECK(sc_container_contents_read(consumer.contents, self, 42, &snapshot) == SCO_OK && snapshot.total_seen == 0);
    f.container = 4; snapshot = {}; snapshot.size = sizeof(snapshot); snapshot.count = 64;
    CHECK(sc_container_contents_read(consumer.contents, self, 42, &snapshot) == SCO_OK && !(snapshot.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN));
    f.container = 3; snapshot = {}; snapshot.size = sizeof(snapshot); snapshot.count = 64;
    CHECK(sc_container_contents_read(consumer.contents, self, 42, &snapshot) == SCO_OK && snapshot.generation == 2);
    f.container = 2; snapshot = {}; snapshot.size = sizeof(snapshot); snapshot.count = 64;
    CHECK(sc_container_contents_read(consumer.contents, self, 42, &snapshot) == SCO_NOT_FOUND && snapshot.count == 0);
    f.container = 0; auto item = consumer.InspectItem(42, 0);
    CHECK(item.result == SCO_OK && item.total == 1 && (item.flags & SC_CONTAINER_CONTENTS_TRUNCATED) && !item.deliverable);
    item = consumer.InspectItem(42, 64); CHECK(item.result == SCO_OK && item.generation == 1 && (item.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN) && !item.deliverable);
    f.container = 3; item = consumer.InspectItem(42, 64, 1); CHECK(item.result == SCO_OK && item.generation == 2 && item.stale && !item.deliverable);
    f.container = 4; item = consumer.InspectItem(42, 64); CHECK(item.result == SCO_OK && !(item.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN) && !item.deliverable);
    f.container = 2; item = consumer.InspectItem(42, 64); CHECK(item.result == SCO_NOT_FOUND && !item.deliverable);
    caps::Set("game.inventory.contents.read", false, "test"); item = consumer.InspectItem(42, 64);
    CHECK(item.result == SCO_UNAVAILABLE && !item.deliverable); caps::Set("game.inventory.contents.read", true, "test");
    auto* foreign = host::NewPlugin("reward_foreign"); CHECK(foreign);
    CHECK(consumer.economy->receipt(foreign, 100, &receipt) == SCO_NOT_FOUND);
    Journal foreign_journal; Consumer foreign_consumer{api, foreign, foreign_journal};
    CHECK(foreign_consumer.Reward(id, 1) == SCO_NOT_FOUND && f.payments == 1);
    CHECK(Release(foreign) == Result::Ok);
    // A stale expected balance is refused before any backend payment attempt.
    Journal mismatch; CHECK(mismatch.CommitIntent(1, id, offer.offer_id, offer.generation, {sizeof(sc_economy_request), 0, 103, 69, 2}));
    Consumer stale{api, self, mismatch}; stale.BindAccepted(id, offer, 1); CHECK(stale.Open());
    CHECK(stale.Reward(id, 1) == SCO_FAILED && f.payments == 1 && mismatch.stage == Journal::Unknown);
    CHECK(stale.economy->receipt(self, 103, &receipt) == SCO_NOT_FOUND);
    // Simulate a committed intent whose external effect was attempted but returned failure.
    Journal uncertain; CHECK(uncertain.CommitIntent(1, id, offer.offer_id, offer.generation, {sizeof(sc_economy_request), 0, 101, 70, 5}));
    Consumer other{api, self, uncertain}; other.BindAccepted(id, offer, 1); CHECK(other.Open()); f.uncertain = true;
    CHECK(other.Reward(id, 1) == SCO_FAILED && f.payments == 2 && f.balance == 75);
    CHECK(other.Reward(id, 1) == SCO_FAILED && f.payments == 2);
    CHECK(other.economy->apply(self, &uncertain.request, &receipt) == SCO_FAILED && f.payments == 2);
    f.uncertain = false;
    Journal lost; lost.fail_receipt_commit = true; Consumer third{api, self, lost}; third.BindAccepted(id, offer, 1); CHECK(third.Open());
    // Different key avoids collision with the first synthetic reward.
    CHECK(lost.CommitIntent(1, id, offer.offer_id, offer.generation, {sizeof(sc_economy_request), 0, 102, 75, 20}));
    CHECK(third.Reward(id, 1) == SCO_FAILED && f.payments == 3 && lost.stage == Journal::Unknown);
    CHECK(third.economy->receipt(self, 102, &receipt) == SCO_OK && receipt.after == 95);
    // A second completed mission in the same session must not inherit the first paid record.
    f.state = SC_MISSION_ACTIVE; f.objective = 0; uint64_t second = 0;
    CHECK(consumer.missions->accept(self, &offer, 8, &second) == SCO_OK && second != id);
    CHECK(consumer.missions->objective(self, second, 0, 0) == SCO_OK);
    CHECK(consumer.missions->advance(self, second, 0) == SCO_OK);
    Consumer second_consumer{api, self, journal}; second_consumer.BindAccepted(second, offer, 1);
    CHECK(second_consumer.Reward(second, 1) == SCO_FAILED && f.payments == 3);
    // Restore only the fake committed state under a new session and owner: it is not proof of payment.
    Journal restored = lost.Restore(); CHECK(restored.stage == Journal::Unknown);
    CHECK(Release(self) == Result::Ok);
    auto* replacement = host::NewPlugin("reward_replacement"); CHECK(replacement);
    Consumer after_restart{api, replacement, restored}; CHECK(after_restart.Open());
    CHECK(after_restart.economy->receipt(replacement, 102, &receipt) == SCO_NOT_FOUND);
    // A new owner in the same session can accept its own synthetic mission, but must not replay an old intent.
    f.state = SC_MISSION_ACTIVE; f.objective = 0;
    sc_mission_offer again{}; again.size = sizeof(again); uint64_t new_id = 0;
    CHECK(after_restart.missions->offer(replacement, 0, &again) == SCO_OK);
    CHECK(after_restart.missions->accept(replacement, &again, 9, &new_id) == SCO_OK);
    after_restart.BindAccepted(new_id, again, 1); f.state = SC_MISSION_COMPLETED;
    CHECK(after_restart.Reward(new_id, 1) == SCO_FAILED && f.payments == 3);
    f.admission.session = 2;
    f.state = SC_MISSION_ACTIVE; f.objective = 0;
    sc_mission_offer fresh{}; fresh.size = sizeof(fresh); uint64_t active = 0;
    CHECK(after_restart.missions->offer(replacement, 0, &fresh) == SCO_OK);
    CHECK(after_restart.missions->accept(replacement, &fresh, 2, &active) == SCO_OK);
    CHECK(after_restart.missions->objective(replacement, active, 0, 0) == SCO_OK);
    CHECK(after_restart.missions->advance(replacement, active, 0) == SCO_OK);
    after_restart.BindAccepted(active, fresh, 2);
    CHECK(after_restart.Reward(active, 2) == SCO_FAILED && f.payments == 3);
    Consumer before_restart{api, replacement, pre_restore}; before_restart.BindAccepted(active, fresh, 2);
    CHECK(before_restart.Reward(active, 2) == SCO_FAILED && f.payments == 3);
    Consumer paid_restart{api, replacement, paid_restore}; paid_restart.BindAccepted(active, fresh, 2);
    CHECK(paid_restart.Reward(active, 2) == SCO_FAILED && f.payments == 3);
    f.state = SC_MISSION_ACTIVE; f.objective = 0; uint64_t cleanup = 0;
    CHECK(after_restart.missions->accept(replacement, &fresh, 3, &cleanup) == SCO_OK);
    C::StopContainerContents(); CHECK(Release(replacement) == Result::Ok && f.abandons == 1); CHECK(M::Stop() == Result::Ok);
    std::puts("reward consumer: PASS (synthetic policy and providers; no game)");
}
