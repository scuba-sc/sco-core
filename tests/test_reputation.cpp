// SPDX-License-Identifier: GPL-3.0-only
// game.reputation 1.0's portable policy (src/game/services/reputation.cpp) against a synthetic
// provider: the default-OFF gate, argument and thread checks, last_error's handshake and owner
// release. Synthetic counts only; no game, no engine, nothing here is in-game evidence.
#include "../src/game/services/reputation.h"
#include <sc_reputation.h>
#include <sco/caps.h>
#include <cstdio>
#include <cstring>
#include <string_view>

using sco::game::services::kReputationCapChecks;
using sco::game::services::Reputation;
using sco::game::services::ReputationProvider;

namespace {

int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

int a, b, dead;
auto* A = reinterpret_cast<sco_plugin*>(&a);
auto* B = reinterpret_cast<sco_plugin*>(&b);
auto* Dead = reinterpret_cast<sco_plugin*>(&dead);

bool gameThread = true;
int checkCalls = 0, logs = 0;
sco_result checkResult = SCO_OK;

bool OnThread() { return gameThread; }
bool Live(const sco_plugin* p) { return p == A || p == B; }
sco_result Checks(sc_reputation_checks* out, const char** why) {
    ++checkCalls;
    if (checkResult != SCO_OK) { *why = "the game faulted reading a reputation check site"; return checkResult; }
    out->size = 0xDEAD;   // the policy, not the provider, owns size
    out->sites = 10;
    out->intact = 9;
    out->services = 1;
    return SCO_OK;
}
void LogCall(const sco_plugin*, const char*, sco_result, const char*) { ++logs; }

ReputationProvider Provider(bool probed, bool rows) {
    ReputationProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    p.rows = rows;
    p.probed = probed;
    p.checks = Checks;
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

std::string_view Error(Reputation& r, sco_plugin* self) {
    static char buf[Reputation::kErrMax];
    uint32_t n = sizeof(buf);
    return r.LastError(self, buf, &n) == SCO_OK ? std::string_view(buf, n - 1) : std::string_view("<none>");
}

sc_reputation_checks Fresh() {
    sc_reputation_checks c{};
    c.size = sizeof(c);
    c.sites = c.intact = c.services = 77;
    return c;
}

Reputation* g_r = nullptr;
sco_result SvcChecks(sco_plugin* s, sc_reputation_checks* out) { return g_r->Checks(s, out); }
sco_result SvcLastError(sco_plugin* s, char* out, uint32_t* n) { return g_r->LastError(s, out, n); }

// Production: rows OK, no in-game run yet. The capability is OFF, nothing reaches the provider.
void TestDefaultOff() {
    Reputation r;
    CHECK(r.Start(Provider(false, true)) == SCO_OK);
    CHECK(!sco::caps::Has(kReputationCapChecks));
    CHECK(Reason(kReputationCapChecks, "OFF pending in-game confirmation"));
    sc_reputation_checks c = Fresh();
    CHECK(r.Checks(A, &c) == SCO_UNAVAILABLE);
    CHECK(c.sites == 0 && c.intact == 0 && c.services == 0 && c.size == sizeof(c));
    CHECK(checkCalls == 0);
    CHECK(Error(r, A).find("OFF pending in-game confirmation") != std::string_view::npos);
    r.Stop();
}

// Rows not OK: OFF whatever probed says, with the rows reason.
void TestRowsNotOk() {
    Reputation r;
    CHECK(r.Start(Provider(true, false)) == SCO_OK);
    CHECK(!sco::caps::Has(kReputationCapChecks));
    CHECK(Reason(kReputationCapChecks, "rows not OK"));
    sc_reputation_checks c = Fresh();
    CHECK(r.Checks(A, &c) == SCO_UNAVAILABLE && checkCalls == 0);
    r.Stop();
}

// A probed provider (no production adapter is one): the gate opens, checks still come first.
void TestProbed() {
    Reputation r;
    g_r = &r;
    const sc_reputation_v1 table{sizeof(sc_reputation_v1), 0, SvcChecks, SvcLastError};
    CHECK(r.Start(Provider(true, true)) == SCO_OK);
    CHECK(sco::caps::Has(kReputationCapChecks));

    sc_reputation_checks c = Fresh();
    CHECK(table.checks(A, &c) == SCO_OK && checkCalls == 1);
    CHECK(c.size == sizeof(c) && c.sites == 10 && c.intact == 9 && c.services == 1);

    const int calls0 = checkCalls;
    CHECK(table.checks(A, nullptr) == SCO_BAD_ARG);
    sc_reputation_checks small = Fresh();
    small.size = sizeof(small) - 1;
    CHECK(table.checks(A, &small) == SCO_BAD_ARG && small.sites == 77);
    CHECK(Error(r, A) == "out is NULL or out->size is too small");
    CHECK(table.checks(Dead, &c) == SCO_BAD_ARG);
    CHECK(table.checks(nullptr, &c) == SCO_BAD_ARG);
    gameThread = false;
    CHECK(table.checks(A, &c) == SCO_WRONG_THREAD);
    CHECK(Error(r, A) == "checks: game thread only");
    gameThread = true;
    CHECK(checkCalls == calls0);

    checkResult = SCO_FAILED;
    c = Fresh();
    CHECK(table.checks(B, &c) == SCO_FAILED && c.sites == 0 && c.intact == 0 && c.services == 0);
    checkResult = SCO_OK;
    CHECK(Error(r, B) == "the game faulted reading a reputation check site");
    CHECK(Error(r, A) == "out is NULL or out->size is too small");
    CHECK(logs > 0);

    // last_error's handshake.
    const uint32_t need = sizeof("out is NULL or out->size is too small");
    uint32_t n = 0;
    CHECK(table.last_error(A, nullptr, &n) == SCO_TOO_MANY && n == need);
    char tiny[4];
    n = sizeof(tiny);
    CHECK(table.last_error(A, tiny, &n) == SCO_TOO_MANY && n == need);
    n = 5;
    CHECK(table.last_error(A, nullptr, &n) == SCO_BAD_ARG);
    CHECK(table.last_error(A, tiny, nullptr) == SCO_BAD_ARG);

    // Owner release forgets only that owner's error.
    r.ReleaseOwner(A);
    CHECK(Error(r, A).empty());
    CHECK(Error(r, B) == "the game faulted reading a reputation check site");

    r.Stop();
    CHECK(!sco::caps::Has(kReputationCapChecks));
    c = Fresh();
    CHECK(table.checks(A, &c) == SCO_UNAVAILABLE);
    CHECK(Error(r, A) == "game.reputation stopped");
    g_r = nullptr;
}

void TestStartOffThread() {
    Reputation r;
    gameThread = false;
    CHECK(r.Start(Provider(true, true)) == SCO_WRONG_THREAD && !r.Started());
    gameThread = true;
}

}  // namespace

int main() {
    TestDefaultOff();
    TestRowsNotOk();
    TestProbed();
    TestStartOffThread();
    std::printf("game.reputation policy: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
