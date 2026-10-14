// SPDX-License-Identifier: GPL-3.0-only
// game.asop 1.0's portable policy (src/game/services/asop_service.cpp) against a synthetic
// provider: the default-OFF gate, argument and thread checks, last_error's handshake and owner
// release. Synthetic values only; no game, no engine, nothing here is in-game evidence.
#include "../src/game/services/asop_service.h"
#include <sc_asop.h>
#include <sco/caps.h>
#include <cstdio>
#include <cstring>
#include <string_view>

using sco::game::services::Asop;
using sco::game::services::AsopProvider;
using sco::game::services::kAsopCapClaimTimeout;
using sco::game::services::kAsopCapShardPersisted;

namespace {

int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

int a, b, dead;
auto* A = reinterpret_cast<sco_plugin*>(&a);
auto* B = reinterpret_cast<sco_plugin*>(&b);
auto* Dead = reinterpret_cast<sco_plugin*>(&dead);

bool gameThread = true;
int claimCalls = 0, shardCalls = 0, logs = 0;
sco_result claimResult = SCO_OK;
uint32_t shardRaw = 0;

bool OnThread() { return gameThread; }
bool Live(const sco_plugin* p) { return p == A || p == B; }
sco_result ClaimTimeout(int32_t* seconds, const char** why) {
    ++claimCalls;
    if (claimResult != SCO_OK) {
        *why = "the insurance configuration isn't loaded yet";
        return claimResult;
    }
    *seconds = 120;
    return SCO_OK;
}
sco_result ShardPersisted(uint32_t* persisted, const char**) {
    ++shardCalls;
    *persisted = shardRaw;
    return SCO_OK;
}
void LogCall(const sco_plugin*, const char*, sco_result, const char*) { ++logs; }

AsopProvider Provider(bool probed, bool claimRows, bool shardRows) {
    AsopProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    p.claim_rows = claimRows;
    p.shard_rows = shardRows;
    p.probed = probed;
    p.claim_timeout = ClaimTimeout;
    p.shard_persisted = ShardPersisted;
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

std::string_view Error(Asop& s, sco_plugin* self) {
    static char buf[Asop::kErrMax];
    uint32_t n = sizeof(buf);
    return s.LastError(self, buf, &n) == SCO_OK ? std::string_view(buf, n - 1) : std::string_view("<none>");
}

Asop* g_s = nullptr;
sco_result SvcClaimTimeout(sco_plugin* p, int32_t* out) { return g_s->ClaimTimeout(p, out); }
sco_result SvcShardPersisted(sco_plugin* p, uint32_t* out) { return g_s->ShardPersisted(p, out); }
sco_result SvcLastError(sco_plugin* p, char* out, uint32_t* n) { return g_s->LastError(p, out, n); }

// Production: rows OK, no in-game run yet. Both capabilities OFF, nothing reaches the provider.
void TestDefaultOff() {
    Asop s;
    CHECK(s.Start(Provider(false, true, true)) == SCO_OK);
    CHECK(!sco::caps::Has(kAsopCapClaimTimeout) && !sco::caps::Has(kAsopCapShardPersisted));
    CHECK(Reason(kAsopCapClaimTimeout, "OFF pending in-game confirmation"));
    CHECK(Reason(kAsopCapShardPersisted, "OFF pending in-game confirmation"));
    int32_t seconds = 7;
    uint32_t persisted = 7;
    CHECK(s.ClaimTimeout(A, &seconds) == SCO_UNAVAILABLE && seconds == 0);
    CHECK(s.ShardPersisted(A, &persisted) == SCO_UNAVAILABLE && persisted == 0);
    CHECK(claimCalls == 0 && shardCalls == 0);
    CHECK(Error(s, A).find("OFF pending in-game confirmation") != std::string_view::npos);
    s.Stop();
}

// Rows not OK: OFF whatever probed says, with the rows reason.
void TestRowsNotOk() {
    Asop s;
    CHECK(s.Start(Provider(true, false, false)) == SCO_OK);
    CHECK(!sco::caps::Has(kAsopCapClaimTimeout) && !sco::caps::Has(kAsopCapShardPersisted));
    CHECK(Reason(kAsopCapClaimTimeout, "rows not OK") && Reason(kAsopCapShardPersisted, "rows not OK"));
    int32_t seconds = 0;
    CHECK(s.ClaimTimeout(A, &seconds) == SCO_UNAVAILABLE && claimCalls == 0);
    s.Stop();
}

// A probed provider (no production adapter is one): the gate opens, checks still come first.
void TestProbed() {
    Asop s;
    g_s = &s;
    const sc_asop_v1 table{sizeof(sc_asop_v1), 0, SvcClaimTimeout, SvcShardPersisted, SvcLastError};
    CHECK(s.Start(Provider(true, true, true)) == SCO_OK);
    CHECK(sco::caps::Has(kAsopCapClaimTimeout) && sco::caps::Has(kAsopCapShardPersisted));

    int32_t seconds = 0;
    CHECK(table.claim_timeout(A, &seconds) == SCO_OK && seconds == 120 && claimCalls == 1);
    claimResult = SCO_NOT_FOUND;
    CHECK(table.claim_timeout(A, &seconds) == SCO_NOT_FOUND && seconds == 0);
    claimResult = SCO_OK;
    CHECK(Error(s, A) == "the insurance configuration isn't loaded yet");

    uint32_t persisted = 9;
    shardRaw = 0;
    CHECK(table.shard_persisted(A, &persisted) == SCO_OK && persisted == 0);
    shardRaw = 0x80;
    CHECK(table.shard_persisted(A, &persisted) == SCO_OK && persisted == 1);

    const int claim0 = claimCalls, shard0 = shardCalls;
    CHECK(table.claim_timeout(A, nullptr) == SCO_BAD_ARG);
    CHECK(Error(s, A) == "out_seconds is NULL");
    CHECK(table.shard_persisted(B, nullptr) == SCO_BAD_ARG);
    CHECK(Error(s, B) == "out_persisted is NULL");
    CHECK(table.claim_timeout(Dead, &seconds) == SCO_BAD_ARG);
    CHECK(table.claim_timeout(nullptr, &seconds) == SCO_BAD_ARG);
    gameThread = false;
    CHECK(table.shard_persisted(A, &persisted) == SCO_WRONG_THREAD);
    CHECK(Error(s, A) == "shard_persisted: game thread only");
    gameThread = true;
    CHECK(claimCalls == claim0 && shardCalls == shard0);
    CHECK(logs > 0);

    // last_error's handshake.
    uint32_t n = 0;
    CHECK(table.last_error(A, nullptr, &n) == SCO_TOO_MANY && n == sizeof("out_seconds is NULL"));
    char small[4];
    n = sizeof(small);
    CHECK(table.last_error(A, small, &n) == SCO_TOO_MANY && n == sizeof("out_seconds is NULL"));
    n = 5;
    CHECK(table.last_error(A, nullptr, &n) == SCO_BAD_ARG);
    CHECK(table.last_error(A, small, nullptr) == SCO_BAD_ARG);

    // Owner release forgets only that owner's error.
    s.ReleaseOwner(A);
    CHECK(Error(s, A).empty());
    CHECK(Error(s, B) == "out_persisted is NULL");

    s.Stop();
    CHECK(!sco::caps::Has(kAsopCapClaimTimeout) && !sco::caps::Has(kAsopCapShardPersisted));
    CHECK(table.claim_timeout(A, &seconds) == SCO_UNAVAILABLE);
    CHECK(Error(s, A) == "game.asop stopped");
    g_s = nullptr;
}

void TestStartOffThread() {
    Asop s;
    gameThread = false;
    CHECK(s.Start(Provider(true, true, true)) == SCO_WRONG_THREAD && !s.Started());
    gameThread = true;
}

}  // namespace

int main() {
    TestDefaultOff();
    TestRowsNotOk();
    TestProbed();
    TestStartOffThread();
    std::printf("game.asop policy: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
