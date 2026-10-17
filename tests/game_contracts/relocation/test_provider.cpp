// SPDX-License-Identifier: GPL-3.0-only
// Synthetic provider fixture for game.relocation: the real provider over a fake backend. No engine access.
#include "sco/host.h"
#include "sco/caps.h"
#include "sc_relocation.h"
#include "sco/game/services.h"
#include "game/services/relocation.h"
#include "game/admission.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace S = sco::game::services;

namespace {
struct World {
    bool offline = true, player = true, streamed = true, moveOk = true, poseValid = false, throwMove = false;
    uint64_t id = 11, instance = 21, system = 7, targetSystem = 7, now = 1000;
    double pos[3] = {1, 2, 3}, rot[4] = {0, 0, 0, 1};
    int moves = 0, offlineCalls = 0, engineReads = 0;
    const sc_relocation_v1* reenter = nullptr;
    sco_plugin* self = nullptr;
    sco_result reenterResult = SCO_OK;
} w;

bool Offline() {
    ++w.offlineCalls;
    if (w.reenter) {
        sc_relocation_request r{sizeof(r), 1000, 5, {1, 2, 3}, {0, 0, 0, 1}};
        uint64_t t = 0;
        w.reenterResult = w.reenter->request(w.self, &r, &t);
    }
    return w.offline;
}
bool Player(S::RelocationPlayer* out) {
    ++w.engineReads;
    if (!w.player) return false;
    out->id = w.id; out->instance = w.instance; out->system = w.system;
    return true;
}
bool Target(uint64_t, S::RelocationTarget* out) {
    ++w.engineReads;
    out->streamed = w.streamed; out->system = w.targetSystem;
    return true;
}
bool Move(const sc_relocation_request*) {
    ++w.moves;
    if (w.throwMove) throw 1;
    return w.moveOk;
}
bool Pose(uint64_t, double pos[3], double rot[4]) {
    if (!w.poseValid) return false;
    for (int i = 0; i < 3; ++i) pos[i] = w.pos[i];
    for (int i = 0; i < 4; ++i) rot[i] = w.rot[i];
    return true;
}
uint64_t Clock() { return w.now; }

S::RelocationBackend Good() {
    S::RelocationBackend b;
    b.verified = true; b.offline = Offline; b.player = Player; b.target = Target; b.move = Move; b.pose = Pose; b.now = Clock;
    return b;
}

const sco_api* api = nullptr;
sco_plugin* self = nullptr;
const sc_relocation_v1* table = nullptr;

bool Cap() { return api->has(SC_RELOCATION_NAME) != 0; }

void Begin(const S::RelocationBackend* backend) {
    S::StopRelocation();
    w = World{};
    w.self = self;
    CHECK(S::StartRelocation(backend) == sco::Result::Ok);
    const void* t = nullptr;
    CHECK(api->query_service(SC_RELOCATION_NAME, SC_RELOCATION_VERSION, &t) == SCO_OK);
    table = static_cast<const sc_relocation_v1*>(t);
    w.reenter = nullptr;
}

sc_relocation_request Req() { return {sizeof(sc_relocation_request), 1000, 5, {1, 2, 3}, {0, 0, 0, 1}}; }

uint64_t Admit() {
    const auto r = Req();
    uint64_t ticket = 0;
    CHECK(table->request(self, &r, &ticket) == SCO_OK && ticket);
    return ticket;
}

sc_relocation_status State(uint64_t ticket) {
    sc_relocation_status s{};
    s.size = sizeof(s);
    CHECK(table->status(self, ticket, &s) == SCO_OK);
    return s;
}

void Refused() {
    const auto r = Req();
    uint64_t ticket = 77;
    CHECK(table->request(self, &r, &ticket) == SCO_UNAVAILABLE && ticket == 77);
}

void Failed(uint64_t ticket, uint32_t reason, uint32_t applied) {
    const auto s = State(ticket);
    CHECK(s.state == SC_RELOCATION_FAILED && s.reason == reason && s.applied == applied);
}
}

int main() {
    sco::SetGameThread();
    api = sco::host::BuildApi({"synthetic relocation host"});
    self = sco::host::NewPlugin("relocation_test");

    // Host socket sco_game_set_relocation_backend: admission is host::FreshAdmissionAllowed, the
    // callbacks are the provider's own (player/target/apply/pose/now), so the provider is usable.
    namespace gh = sco::game::host;
    static gh::Admission evidence;
    static int socketMoves = 0;
    const auto queryTable = [&]() {
        const void* t = nullptr;
        CHECK(api->query_service(SC_RELOCATION_NAME, SC_RELOCATION_VERSION, &t) == SCO_OK);
        return static_cast<const sc_relocation_v1*>(t);
    };
    const auto notInstalled = [&]() {
        const void* t = nullptr;
        return api->query_service(SC_RELOCATION_NAME, SC_RELOCATION_VERSION, &t) == SCO_NOT_FOUND && !t && !Cap();
    };
    w = World{};
    sco_relocation_backend socket{sizeof(socket), 0, Player, Target,
                                  [](const sc_relocation_request*) { ++socketMoves; return true; }, Pose, Clock};
    // Null and malformed providers are refused before anything is published.
    CHECK(sco_game_set_relocation_backend(nullptr) == SCO_BAD_ARG);
    {
        sco_relocation_backend bad = socket;
        bad.size = sizeof(bad) - 1; CHECK(sco_game_set_relocation_backend(&bad) == SCO_BAD_ARG);
        bad = socket; bad.player = nullptr; CHECK(sco_game_set_relocation_backend(&bad) == SCO_BAD_ARG);
        bad = socket; bad.target = nullptr; CHECK(sco_game_set_relocation_backend(&bad) == SCO_BAD_ARG);
        bad = socket; bad.apply = nullptr; CHECK(sco_game_set_relocation_backend(&bad) == SCO_BAD_ARG);
        bad = socket; bad.pose = nullptr; CHECK(sco_game_set_relocation_backend(&bad) == SCO_BAD_ARG);
    }
    CHECK(notInstalled());
    {
        sco_result viaThread = SCO_OK;
        std::thread([&] { viaThread = sco_game_set_relocation_backend(&socket); }).join();
        CHECK(viaThread == SCO_WRONG_THREAD && notInstalled());
    }
    // Default admission (host never composed one): installed, but the capability is off and requests refuse.
    CHECK(sco_game_set_relocation_backend(&socket) == SCO_OK);
    table = queryTable();
    S::TickRelocation();
    CHECK(!Cap());
    Refused();
    CHECK(w.engineReads == 0 && socketMoves == 0);
    S::StopRelocation();
    // Enabled admission: unknown evidence refuses, positive evidence admits and the move is observed.
    gh::detail::SetFreshInspector([]() { return evidence; });
    gh::detail::SetFreshAdmissionEnabled(true);
    CHECK(sco_game_set_relocation_backend(&socket) == SCO_OK);
    table = queryTable();
    S::TickRelocation();
    CHECK(!Cap());
    Refused();
    evidence = {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
    S::TickRelocation();
    CHECK(Cap());
    uint64_t sockTicket = Admit();
    w.now = 1100;
    S::TickRelocation();
    CHECK(socketMoves == 1 && State(sockTicket).applied == 1 && State(sockTicket).state == SC_RELOCATION_PENDING);
    w.poseValid = true;
    w.pos[0] = 1.0; w.rot[2] = 0;
    S::TickRelocation();
    CHECK(State(sockTicket).state == SC_RELOCATION_COMPLETE && socketMoves == 1);
    // A repeated install is refused and the first provider (and its ticket) stay untouched.
    static int secondMoves = 0;
    sco_relocation_backend rival = socket;
    rival.apply = [](const sc_relocation_request*) { ++secondMoves; return true; };
    CHECK(sco_game_set_relocation_backend(&rival) == SCO_UNAVAILABLE);
    CHECK(Cap() && State(sockTicket).state == SC_RELOCATION_COMPLETE);
    w.poseValid = false;
    sockTicket = Admit();
    w.now = 1200;
    S::TickRelocation();
    CHECK(socketMoves == 2 && secondMoves == 0);
    // Live revocation: the in-flight request fails on safety before any further move and requests refuse.
    evidence.session = gh::Session::Online;
    w.now = 1300;
    S::TickRelocation();
    CHECK(!Cap());
    Failed(sockTicket, SC_RELOCATION_REASON_SAFETY, 1);
    Refused();
    CHECK(socketMoves == 2);
    S::StopRelocation();
    // A late install after the production start (engine backend, unverified here) is refused, not a no-op OK.
    CHECK(S::StartRelocation() == sco::Result::Ok);
    CHECK(sco_game_set_relocation_backend(&socket) == SCO_UNAVAILABLE);
    S::TickRelocation();
    CHECK(!Cap() && socketMoves == 2);
    S::StopRelocation();
    gh::detail::SetFreshAdmissionEnabled(false);
    S::RelocationBackend good = Good();

    Begin(nullptr);
    CHECK(!Cap());
    Refused();
    CHECK(w.offlineCalls == 0 && w.engineReads == 0);

    S::RelocationBackend unverified = good;
    unverified.verified = false;
    Begin(&unverified);
    CHECK(!Cap());
    Refused();
    S::TickRelocation();
    CHECK(!Cap() && w.offlineCalls == 0 && w.engineReads == 0);

    for (int missing = 0; missing < 5; ++missing) {
        S::RelocationBackend partial = good;
        if (missing == 0) partial.offline = nullptr;
        if (missing == 1) partial.player = nullptr;
        if (missing == 2) partial.target = nullptr;
        if (missing == 3) partial.move = nullptr;
        if (missing == 4) partial.pose = nullptr;
        Begin(&partial);
        S::TickRelocation();
        CHECK(!Cap());
        Refused();
        CHECK(w.moves == 0);
    }

    Begin(&good);
    CHECK(Cap());
    CHECK(S::RelocationEnabled());
    {
        sc_relocation_request bad = Req();
        bad.size = sizeof(bad) - 1;
        uint64_t ticket = 77;
        CHECK(table->request(self, &bad, &ticket) == SCO_BAD_ARG && ticket == 77);
    }

    const auto refuseWith = [&](const char* name, void (*mutate)()) {
        Begin(&good);
        mutate();
        const auto r = Req();
        uint64_t ticket = 77;
        const sco_result result = table->request(self, &r, &ticket);
        if (result != SCO_UNAVAILABLE || ticket != 77 || w.moves != 0) { std::printf("FAIL refusal %s\n", name); std::exit(1); }
    };
    refuseWith("offline", [] { w.offline = false; });
    refuseWith("no player", [] { w.player = false; });
    refuseWith("player id 0", [] { w.id = 0; });
    refuseWith("player instance 0", [] { w.instance = 0; });
    refuseWith("current system unknown", [] { w.system = 0; });
    refuseWith("target system unknown", [] { w.targetSystem = 0; });
    refuseWith("other system", [] { w.targetSystem = 8; });
    refuseWith("not streamed", [] { w.streamed = false; });

    Begin(&good);
    w.offline = false;
    S::TickRelocation();
    CHECK(!Cap());
    const int readsBefore = w.engineReads;
    S::TickRelocation();
    Refused();
    CHECK(w.engineReads == readsBefore);
    w.offline = true;
    S::TickRelocation();
    CHECK(Cap());

    Begin(&good);
    S::SetRelocationEnabled(false);
    CHECK(!S::RelocationEnabled());
    {
        const int offlineBefore = w.offlineCalls, readsNow = w.engineReads;
        S::TickRelocation();
        CHECK(!Cap());
        Refused();
        CHECK(w.offlineCalls == offlineBefore && w.engineReads == readsNow && w.moves == 0);
    }
    w.offline = false;
    S::SetRelocationEnabled(true);
    S::TickRelocation();
    CHECK(!Cap());
    Refused();
    w.offline = true;
    S::TickRelocation();
    CHECK(Cap());

    Begin(&good);
    uint64_t ticket = Admit();
    CHECK(State(ticket).state == SC_RELOCATION_PENDING && State(ticket).applied == 0);
    w.now = 1100;
    S::TickRelocation();
    CHECK(w.moves == 1 && State(ticket).applied == 1 && State(ticket).state == SC_RELOCATION_PENDING);
    w.now = 1200;
    S::TickRelocation();
    CHECK(w.moves == 1 && State(ticket).state == SC_RELOCATION_PENDING);
    w.poseValid = true;
    w.pos[0] = 1.06;
    S::TickRelocation();
    CHECK(State(ticket).state == SC_RELOCATION_PENDING);
    w.pos[0] = 1.0;
    w.rot[2] = 1;
    S::TickRelocation();
    CHECK(State(ticket).state == SC_RELOCATION_PENDING);
    w.rot[2] = 0;
    S::TickRelocation();
    CHECK(State(ticket).state == SC_RELOCATION_COMPLETE && State(ticket).applied == 1 && w.moves == 1);
    CHECK(table->cancel(self, ticket) == SCO_OK && State(ticket).state == SC_RELOCATION_COMPLETE);
    CHECK(Cap());

    Begin(&good);
    ticket = Admit();
    w.streamed = false;
    w.now = 1100;
    S::TickRelocation();
    CHECK(w.moves == 0 && State(ticket).state == SC_RELOCATION_PENDING);
    w.streamed = true;
    w.now = 1200;
    S::TickRelocation();
    CHECK(w.moves == 1 && State(ticket).applied == 1);

    Begin(&good);
    ticket = Admit();
    w.streamed = false;
    w.now = 1999;
    S::TickRelocation();
    CHECK(State(ticket).state == SC_RELOCATION_PENDING);
    w.now = 2000;
    S::TickRelocation();
    Failed(ticket, SC_RELOCATION_REASON_TIMEOUT, 0);
    CHECK(w.moves == 0);

    Begin(&good);
    ticket = Admit();
    w.now = 1100;
    S::TickRelocation();
    CHECK(State(ticket).applied == 1);
    w.now = 2000;
    S::TickRelocation();
    Failed(ticket, SC_RELOCATION_REASON_TIMEOUT, 1);

    const auto failBeforeMove = [&](const char* name, void (*mutate)(), uint32_t reason) {
        Begin(&good);
        const uint64_t t = Admit();
        mutate();
        w.now = 1100;
        S::TickRelocation();
        const auto s = State(t);
        if (s.state != SC_RELOCATION_FAILED || s.reason != reason || s.applied != 0 || w.moves != 0) {
            std::printf("FAIL in-flight %s\n", name); std::exit(1);
        }
    };
    failBeforeMove("offline lost", [] { w.offline = false; }, SC_RELOCATION_REASON_SAFETY);
    failBeforeMove("switched off", [] { S::SetRelocationEnabled(false); }, SC_RELOCATION_REASON_SAFETY);
    S::SetRelocationEnabled(true);
    failBeforeMove("player entity changed", [] { w.instance = 22; }, SC_RELOCATION_REASON_SESSION);
    failBeforeMove("player id changed", [] { w.id = 12; }, SC_RELOCATION_REASON_SESSION);
    failBeforeMove("player lost", [] { w.player = false; }, SC_RELOCATION_REASON_SESSION);
    failBeforeMove("current system changed", [] { w.system = 8; w.targetSystem = 7; }, SC_RELOCATION_REASON_SYSTEM);
    failBeforeMove("current system unknown", [] { w.system = 0; }, SC_RELOCATION_REASON_SYSTEM);
    failBeforeMove("target system changed", [] { w.targetSystem = 8; }, SC_RELOCATION_REASON_SYSTEM);
    failBeforeMove("target system unknown", [] { w.targetSystem = 0; }, SC_RELOCATION_REASON_SYSTEM);

    Begin(&good);
    ticket = Admit();
    w.player = false;
    S::TickRelocation();
    w.player = true;
    S::TickRelocation();
    Failed(ticket, SC_RELOCATION_REASON_SESSION, 0);
    CHECK(w.moves == 0);

    Begin(&good);
    ticket = Admit();
    w.now = 1100;
    S::TickRelocation();
    CHECK(State(ticket).applied == 1);
    w.instance = 22;
    S::TickRelocation();
    Failed(ticket, SC_RELOCATION_REASON_SESSION, 1);
    CHECK(Cap());

    Begin(&good);
    ticket = Admit();
    w.moveOk = false;
    w.now = 1100;
    S::TickRelocation();
    Failed(ticket, SC_RELOCATION_REASON_TRANSPORT, 0);
    CHECK(w.moves == 1 && Cap());
    w.now = 1200;
    S::TickRelocation();
    CHECK(w.moves == 1);

    Begin(&good);
    ticket = Admit();
    CHECK(table->cancel(self, ticket) == SCO_OK);
    w.now = 1100;
    S::TickRelocation();
    CHECK(w.moves == 0 && State(ticket).state == SC_RELOCATION_CANCELLED);

    Begin(&good);
    ticket = Admit();
    uint64_t second = 55;
    {
        const auto r = Req();
        CHECK(table->request(self, &r, &second) == SCO_TOO_MANY && second == 55);
    }
    S::StopRelocation();
    S::TickRelocation();
    CHECK(!Cap() && w.moves == 0);
    {
        const auto r = Req();
        const void* gone = nullptr;
        CHECK(api->query_service(SC_RELOCATION_NAME, SC_RELOCATION_VERSION, &gone) == SCO_NOT_FOUND && !gone);
        (void)r;
    }

    S::RelocationBackend noClock = good;
    noClock.now = nullptr;
    Begin(&noClock);
    ticket = Admit();
    CHECK(State(ticket).state == SC_RELOCATION_PENDING);

    Begin(&good);
    {
        sco_result viaOwner = SCO_OK;
        std::thread other([&] {
            const auto r = Req();
            uint64_t t = 77;
            viaOwner = table->request(self, &r, &t);
            CHECK(t == 77);
        });
        other.join();
        CHECK(viaOwner == SCO_WRONG_THREAD);
    }

    Begin(&good);
    w.reenter = table;
    S::TickRelocation();
    CHECK(w.reenterResult == SCO_UNAVAILABLE);
    w.reenter = nullptr;

    Begin(&good);
    ticket = Admit();
    w.throwMove = true;
    w.now = 1100;
    S::TickRelocation();
    Failed(ticket, SC_RELOCATION_REASON_FAULT, 0);
    CHECK(!Cap());
    w.throwMove = false;
    Refused();
    S::StopRelocation();
    Begin(&good);
    S::TickRelocation();
    CHECK(!Cap());
    Refused();
    CHECK(w.moves == 0);

    S::StopRelocation();

    std::puts("relocation provider: OK");
    return 0;
}
