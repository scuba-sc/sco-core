// SPDX-License-Identifier: GPL-3.0-only
// game.contracts 1.0's portable policy (src/game/services/contracts_service.cpp) against a
// synthetic provider: the default-OFF gate, argument and thread checks, last_error's handshake and
// owner release. Synthetic values only; no game, no engine, nothing here is in-game evidence.
#include "../src/game/services/contracts_service.h"
#include <sc_contracts.h>
#include <sco/caps.h>
#include <cstdio>
#include <cstring>
#include <string_view>

using sco::game::services::Contracts;
using sco::game::services::ContractsProvider;
using sco::game::services::kContractsCapStreamRadius;

namespace {

int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

int a, b, dead;
auto* A = reinterpret_cast<sco_plugin*>(&a);
auto* B = reinterpret_cast<sco_plugin*>(&b);
auto* Dead = reinterpret_cast<sco_plugin*>(&dead);

bool gameThread = true;
int radiusCalls = 0, logs = 0;
sco_result radiusResult = SCO_OK;

bool OnThread() { return gameThread; }
bool Live(const sco_plugin* p) { return p == A || p == B; }
sco_result StreamRadius(float* radius, const char** why) {
    ++radiusCalls;
    if (radiusResult != SCO_OK) {
        *why = "the mission settings aren't loaded yet";
        return radiusResult;
    }
    *radius = 1234.5f;
    return SCO_OK;
}
void LogCall(const sco_plugin*, const char*, sco_result, const char*) { ++logs; }

ContractsProvider Provider(bool probed, bool radiusRows) {
    ContractsProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    p.radius_rows = radiusRows;
    p.probed = probed;
    p.stream_radius = StreamRadius;
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

std::string_view Error(Contracts& s, sco_plugin* self) {
    static char buf[Contracts::kErrMax];
    uint32_t n = sizeof(buf);
    return s.LastError(self, buf, &n) == SCO_OK ? std::string_view(buf, n - 1) : std::string_view("<none>");
}

Contracts* g_s = nullptr;
sco_result SvcStreamRadius(sco_plugin* p, float* out) { return g_s->StreamRadius(p, out); }
sco_result SvcLastError(sco_plugin* p, char* out, uint32_t* n) { return g_s->LastError(p, out, n); }

// Production: rows OK, no in-game run yet. The capability is OFF, nothing reaches the provider.
void TestDefaultOff() {
    Contracts s;
    CHECK(s.Start(Provider(false, true)) == SCO_OK);
    CHECK(!sco::caps::Has(kContractsCapStreamRadius));
    CHECK(Reason(kContractsCapStreamRadius, "OFF pending in-game confirmation"));
    float radius = 7.0f;
    CHECK(s.StreamRadius(A, &radius) == SCO_UNAVAILABLE && radius == 0.0f);
    CHECK(radiusCalls == 0);
    CHECK(Error(s, A).find("OFF pending in-game confirmation") != std::string_view::npos);
    s.Stop();
}

// Rows not OK: OFF whatever probed says, with the rows reason.
void TestRowsNotOk() {
    Contracts s;
    CHECK(s.Start(Provider(true, false)) == SCO_OK);
    CHECK(!sco::caps::Has(kContractsCapStreamRadius));
    CHECK(Reason(kContractsCapStreamRadius, "rows not OK"));
    float radius = 0.0f;
    CHECK(s.StreamRadius(A, &radius) == SCO_UNAVAILABLE && radiusCalls == 0);
    s.Stop();
}

// A probed provider (no production adapter is one): the gate opens, checks still come first.
void TestProbed() {
    Contracts s;
    g_s = &s;
    const sc_contracts_v1 table{sizeof(sc_contracts_v1), 0, SvcStreamRadius, SvcLastError};
    CHECK(s.Start(Provider(true, true)) == SCO_OK);
    CHECK(sco::caps::Has(kContractsCapStreamRadius));

    float radius = 0.0f;
    CHECK(table.stream_radius(A, &radius) == SCO_OK && radius == 1234.5f && radiusCalls == 1);
    radiusResult = SCO_NOT_FOUND;
    CHECK(table.stream_radius(A, &radius) == SCO_NOT_FOUND && radius == 0.0f);
    radiusResult = SCO_OK;
    CHECK(Error(s, A) == "the mission settings aren't loaded yet");

    const int calls0 = radiusCalls;
    CHECK(table.stream_radius(A, nullptr) == SCO_BAD_ARG);
    CHECK(Error(s, A) == "out_radius is NULL");
    CHECK(table.stream_radius(B, nullptr) == SCO_BAD_ARG);
    CHECK(Error(s, B) == "out_radius is NULL");
    CHECK(table.stream_radius(Dead, &radius) == SCO_BAD_ARG);
    CHECK(table.stream_radius(nullptr, &radius) == SCO_BAD_ARG);
    gameThread = false;
    CHECK(table.stream_radius(A, &radius) == SCO_WRONG_THREAD);
    CHECK(Error(s, A) == "stream_radius: game thread only");
    gameThread = true;
    CHECK(radiusCalls == calls0);
    CHECK(logs > 0);

    // last_error's handshake.
    uint32_t n = 0;
    CHECK(table.last_error(A, nullptr, &n) == SCO_TOO_MANY && n == sizeof("out_radius is NULL"));
    char small[4];
    n = sizeof(small);
    CHECK(table.last_error(A, small, &n) == SCO_TOO_MANY && n == sizeof("out_radius is NULL"));
    n = 5;
    CHECK(table.last_error(A, nullptr, &n) == SCO_BAD_ARG);
    CHECK(table.last_error(A, small, nullptr) == SCO_BAD_ARG);

    // Owner release forgets only that owner's error.
    s.ReleaseOwner(A);
    CHECK(Error(s, A).empty());
    CHECK(Error(s, B) == "out_radius is NULL");

    s.Stop();
    CHECK(!sco::caps::Has(kContractsCapStreamRadius));
    CHECK(table.stream_radius(A, &radius) == SCO_UNAVAILABLE);
    CHECK(Error(s, A) == "game.contracts stopped");
    g_s = nullptr;
}

void TestStartOffThread() {
    Contracts s;
    gameThread = false;
    CHECK(s.Start(Provider(true, true)) == SCO_WRONG_THREAD && !s.Started());
    gameThread = true;
}

}  // namespace

int main() {
    TestDefaultOff();
    TestRowsNotOk();
    TestProbed();
    TestStartOffThread();
    std::printf("game.contracts policy: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
