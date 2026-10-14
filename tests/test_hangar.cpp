// SPDX-License-Identifier: GPL-3.0-only
// game.hangar 1.0's portable policy (src/game/services/hangar.cpp) against a synthetic provider:
// the default-OFF gate, argument and thread checks, last_error's handshake and owner release.
// Synthetic ids only; no game, no engine, nothing here is in-game evidence.
#include "../src/game/services/hangar.h"
#include <sc_hangar.h>
#include <sco/caps.h>
#include <cstdio>
#include <cstring>
#include <string_view>

using sco::game::services::Hangar;
using sco::game::services::HangarProvider;
using sco::game::services::kHangarCapAtcCancel;
using sco::game::services::kHangarCapLift;

namespace {

int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

int a, b, dead;
auto* A = reinterpret_cast<sco_plugin*>(&a);
auto* B = reinterpret_cast<sco_plugin*>(&b);
auto* Dead = reinterpret_cast<sco_plugin*>(&dead);

bool gameThread = true;
int areaCalls = 0, liftCalls = 0, atcCalls = 0, logs = 0;
uint32_t lastOp = 0;
sco_result liftResult = SCO_OK;

bool OnThread() { return gameThread; }
bool Live(const sco_plugin* p) { return p == A || p == B; }
sco_result LiftOfArea(uint64_t area, uint64_t* lift, const char** why) {
    ++areaCalls;
    if (area == 404) { *why = "no ship lift goes with that landing area"; return SCO_NOT_FOUND; }
    *lift = area + 1000;
    return SCO_OK;
}
sco_result LiftRequest(uint64_t, uint32_t op, const char** why) {
    ++liftCalls;
    lastOp = op;
    if (liftResult != SCO_OK) *why = "the game faulted in the lift request";
    return liftResult;
}
sco_result AtcCancel(uint64_t, const char** why) {
    ++atcCalls;
    *why = "you're not spawned";
    return SCO_FAILED;
}
void LogCall(const sco_plugin*, const char*, sco_result, const char*) { ++logs; }

HangarProvider Provider(bool probed, bool liftRows, bool atcRows) {
    HangarProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    p.lift_rows = liftRows;
    p.atc_rows = atcRows;
    p.probed = probed;
    p.lift_of_area = LiftOfArea;
    p.lift_request = LiftRequest;
    p.atc_cancel = AtcCancel;
    p.log = LogCall;
    return p;
}

bool Reason(const char* cap, const char* needle) {
    sco::caps::Entry e[sco::caps::kMaxCaps];
    const size_t n = sco::caps::List(e, sco::caps::kMaxCaps);
    for (size_t i = 0; i < n && i < sco::caps::kMaxCaps; ++i)
        if (std::strcmp(e[i].name, cap) == 0) return std::strstr(e[i].reason, needle) != nullptr;
    return false;
}

std::string_view Error(Hangar& h, sco_plugin* self) {
    static char buf[Hangar::kErrMax];
    uint32_t n = sizeof(buf);
    return h.LastError(self, buf, &n) == SCO_OK ? std::string_view(buf, n - 1) : std::string_view("<none>");
}

Hangar* g_h = nullptr;
sco_result SvcLiftOfLandingArea(sco_plugin* s, uint64_t area, uint64_t* out) { return g_h->LiftOfLandingArea(s, area, out); }
sco_result SvcLiftRequest(sco_plugin* s, uint64_t lift, uint32_t op) { return g_h->LiftRequest(s, lift, op); }
sco_result SvcAtcCancelRequest(sco_plugin* s, uint64_t atc) { return g_h->AtcCancelRequest(s, atc); }
sco_result SvcLastError(sco_plugin* s, char* out, uint32_t* n) { return g_h->LastError(s, out, n); }

// Production: rows OK, no in-game run yet. Both capabilities OFF, nothing reaches the provider.
void TestDefaultOff() {
    Hangar h;
    CHECK(h.Start(Provider(false, true, true)) == SCO_OK);
    CHECK(!sco::caps::Has(kHangarCapLift) && !sco::caps::Has(kHangarCapAtcCancel));
    CHECK(Reason(kHangarCapLift, "OFF pending in-game confirmation"));
    CHECK(Reason(kHangarCapAtcCancel, "OFF pending in-game confirmation"));
    uint64_t lift = 7;
    CHECK(h.LiftOfLandingArea(A, 10, &lift) == SCO_UNAVAILABLE && lift == 0);
    CHECK(h.LiftRequest(A, 1010, SC_HANGAR_LIFT_OPEN) == SCO_UNAVAILABLE);
    CHECK(h.AtcCancelRequest(A, 20) == SCO_UNAVAILABLE);
    CHECK(areaCalls == 0 && liftCalls == 0 && atcCalls == 0);
    CHECK(Error(h, A).find("OFF pending in-game confirmation") != std::string_view::npos);
    h.Stop();
}

// Rows not OK: OFF whatever probed says, with the rows reason.
void TestRowsNotOk() {
    Hangar h;
    CHECK(h.Start(Provider(true, false, false)) == SCO_OK);
    CHECK(!sco::caps::Has(kHangarCapLift) && !sco::caps::Has(kHangarCapAtcCancel));
    CHECK(Reason(kHangarCapLift, "rows not OK"));
    CHECK(h.LiftRequest(A, 1010, SC_HANGAR_LIFT_CLOSE) == SCO_UNAVAILABLE && liftCalls == 0);
    h.Stop();
}

// A probed provider (no production adapter is one): the gate opens, checks still come first.
void TestProbed() {
    Hangar h;
    g_h = &h;
    const sc_hangar_v1 table{sizeof(sc_hangar_v1), 0, SvcLiftOfLandingArea, SvcLiftRequest, SvcAtcCancelRequest, SvcLastError};
    CHECK(h.Start(Provider(true, true, true)) == SCO_OK);
    CHECK(sco::caps::Has(kHangarCapLift) && sco::caps::Has(kHangarCapAtcCancel));

    uint64_t lift = 0;
    CHECK(table.lift_of_landing_area(A, 10, &lift) == SCO_OK && lift == 1010 && areaCalls == 1);
    CHECK(table.lift_of_landing_area(A, 404, &lift) == SCO_NOT_FOUND && lift == 0);
    CHECK(Error(h, A) == "no ship lift goes with that landing area");
    CHECK(table.lift_request(A, 1010, SC_HANGAR_LIFT_OPEN) == SCO_OK && lastOp == SC_HANGAR_LIFT_OPEN);
    CHECK(table.lift_request(A, 1010, SC_HANGAR_LIFT_CLOSE) == SCO_OK && lastOp == SC_HANGAR_LIFT_CLOSE);

    const int area0 = areaCalls, lift0 = liftCalls;
    CHECK(table.lift_of_landing_area(A, 0, &lift) == SCO_BAD_ARG);
    CHECK(table.lift_of_landing_area(A, 10, nullptr) == SCO_BAD_ARG);
    CHECK(table.lift_request(A, 0, SC_HANGAR_LIFT_OPEN) == SCO_BAD_ARG);
    CHECK(table.lift_request(A, 1010, 0) == SCO_BAD_ARG);
    CHECK(table.lift_request(A, 1010, 3) == SCO_BAD_ARG);
    CHECK(table.atc_cancel_request(A, 0) == SCO_BAD_ARG);
    CHECK(Error(h, A) == "atc_id is 0");
    CHECK(table.lift_request(Dead, 1010, SC_HANGAR_LIFT_OPEN) == SCO_BAD_ARG);
    CHECK(table.lift_request(nullptr, 1010, SC_HANGAR_LIFT_OPEN) == SCO_BAD_ARG);
    gameThread = false;
    CHECK(table.lift_request(A, 1010, SC_HANGAR_LIFT_OPEN) == SCO_WRONG_THREAD);
    CHECK(Error(h, A) == "lift_request: game thread only");
    gameThread = true;
    CHECK(areaCalls == area0 && liftCalls == lift0);

    liftResult = SCO_FAILED;
    CHECK(table.lift_request(B, 1010, SC_HANGAR_LIFT_OPEN) == SCO_FAILED);
    liftResult = SCO_OK;
    CHECK(Error(h, B) == "the game faulted in the lift request");
    CHECK(Error(h, A) == "atc_id is 0");
    CHECK(table.atc_cancel_request(A, 20) == SCO_FAILED && atcCalls == 1);
    CHECK(Error(h, A) == "you're not spawned");
    CHECK(logs > 0);

    // last_error's handshake.
    uint32_t n = 0;
    CHECK(table.last_error(A, nullptr, &n) == SCO_TOO_MANY && n == sizeof("you're not spawned"));
    char small[4];
    n = sizeof(small);
    CHECK(table.last_error(A, small, &n) == SCO_TOO_MANY && n == sizeof("you're not spawned"));
    n = 5;
    CHECK(table.last_error(A, nullptr, &n) == SCO_BAD_ARG);
    CHECK(table.last_error(A, small, nullptr) == SCO_BAD_ARG);

    // Owner release forgets only that owner's error.
    h.ReleaseOwner(A);
    CHECK(Error(h, A).empty());
    CHECK(Error(h, B) == "the game faulted in the lift request");

    h.Stop();
    CHECK(!sco::caps::Has(kHangarCapLift) && !sco::caps::Has(kHangarCapAtcCancel));
    CHECK(table.lift_request(A, 1010, SC_HANGAR_LIFT_OPEN) == SCO_UNAVAILABLE);
    CHECK(Error(h, A) == "game.hangar stopped");
    g_h = nullptr;
}

void TestStartOffThread() {
    Hangar h;
    gameThread = false;
    CHECK(h.Start(Provider(true, true, true)) == SCO_WRONG_THREAD && !h.Started());
    gameThread = true;
}

}  // namespace

int main() {
    TestDefaultOff();
    TestRowsNotOk();
    TestProbed();
    TestStartOffThread();
    std::printf("game.hangar policy: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
