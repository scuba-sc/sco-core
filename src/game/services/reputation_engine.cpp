// SPDX-License-Identifier: GPL-3.0-only
// game.reputation 1.0 on Windows: the ReputationProvider built from rows, and the published table.
// It reads only what the contracts.reputation rows locate (include/sco/game/features.h,
// docs/game/features.md): the services global the reputation-service checks load
// (contracts.reputation_services) and each check site (contracts.reputation_check.1 ..
// .kReputationChecks). No calls into the game, no hooks, no patches, no scans: this file only
// reads. The capability stays OFF (ReputationProvider::probed is false) until an in-game run
// confirms it.
#include "reputation.h"
#include "../../api/internal.h"
#include "sco/game/features.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include <cstdio>
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

// The two check forms the contracts.reputation_check.* rows match, as features_sigs.cpp
// (kReputationForms) declares them; keep the two in step. A site that still matches one of them
// holds the game's own instructions from the services load through the check's call, so the
// patch contracts writes at +features::kReputationPatch is not there.
constexpr const char* kCheckForms[] = {
    "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 18 48 8B 08 48 8B 51 58 48 8B C8 FF D2",
    "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 18 48 8B 08 4C 8B 41 58 48 8B C8 41 FF D0",
};

constexpr int kMaxChecks = features::kReputationChecks;
constexpr char kServicesRow[] = "contracts.reputation_services";

struct Engine {
    const uint8_t* services = nullptr;
    const uint8_t* sites[kMaxChecks] = {};
};
Engine     g_e;
Reputation g_reputation;
bool       g_published = false;

bool RowOk(const SignatureSnapshot* signatures, const char* id) {
    const SigResult* s = signatures ? signatures->Lookup(id) : nullptr;
    return s && s->state == SigState::Ok;
}

void CheckRowId(int k, char* out, size_t n) { std::snprintf(out, n, "contracts.reputation_check.%d", k); }

bool RowsOk() {
    const auto signatures = AcquireHostSignatureSnapshot();
    if (!RowOk(signatures.get(), kServicesRow)) return false;
    char id[64];
    for (int k = 1; k <= kMaxChecks; ++k) {
        CheckRowId(k, id, sizeof(id));
        if (!RowOk(signatures.get(), id)) return false;
    }
    return true;
}

// ---- game reads, under SEH ---------------------------------------------------------------------
//
// 1 yes, 0 no, -1 the game faulted. No C++ object with a destructor lives in these frames.

int SiteIntact(const uint8_t* site) {
    __try {
        for (const char* form : kCheckForms)
            if (BytesMatch(site, form)) return 1;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int ServicesSet(const uint8_t* global) {
    __try {
        return *reinterpret_cast<const uintptr_t*>(global) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// ---- the provider ------------------------------------------------------------------------------

sco_result Checks(sc_reputation_checks* out, const char** why) {
    uint32_t sites = 0, intact = 0;
    for (const uint8_t* site : g_e.sites) {
        if (!site) continue;
        ++sites;
        const int ok = SiteIntact(site);
        if (ok < 0) { *why = "the game faulted reading a reputation check site"; return SCO_FAILED; }
        intact += static_cast<uint32_t>(ok);
    }
    const int set = ServicesSet(g_e.services);
    if (set < 0) { *why = "the game faulted reading the services global"; return SCO_FAILED; }
    out->sites = sites;
    out->intact = intact;
    out->services = static_cast<uint32_t>(set);
    return SCO_OK;
}

bool OnThread() { return OnGameThread(); }
bool Live(const sco_plugin* p) { return host::PluginId(p) && !detail::Released(p); }

void LogCall(const sco_plugin* self, const char* fn, sco_result r, const char* why) {
    const char* id = self ? host::PluginId(self) : nullptr;
    Log("[game] reputation: %s: %s -> %s: %s", id ? id : "?", fn, ResultName(static_cast<Result>(r)), why);
}

sco_result SvcChecks(sco_plugin* self, sc_reputation_checks* out) { return g_reputation.Checks(self, out); }
sco_result SvcLastError(sco_plugin* self, char* out, uint32_t* inoutSize) { return g_reputation.LastError(self, out, inoutSize); }

const sc_reputation_v1 kService = {
    sizeof(sc_reputation_v1), 0, SvcChecks, SvcLastError,
};

}  // namespace

Result StartReputation() {
    if (g_published) return Result::Ok;
    if (!OnGameThread()) return Result::WrongThread;
    ReputationProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    p.rows = RowsOk();
    p.probed = false;
    g_e = {};
    if (p.rows) {
        g_e.services = Sig(kServicesRow);
        char id[64];
        for (int k = 1; k <= kMaxChecks; ++k) {
            CheckRowId(k, id, sizeof(id));
            g_e.sites[k - 1] = Sig(id);
        }
    }
    p.checks = Checks;
    p.log = LogCall;
    if (g_reputation.Start(p) != SCO_OK) {
        g_e = {};
        return Result::WrongThread;
    }
    const Result r = host::ProvideGameService(SC_REPUTATION_SERVICE_NAME, SC_REPUTATION_SERVICE_VERSION, &kService);
    if (r != Result::Ok) {
        g_reputation.Stop();
        g_e = {};
        return r;
    }
    g_published = true;
    Log("[game] game.reputation 1.0 published: game.reputation.checks held OFF pending an in-game run");
    return Result::Ok;
}

void StopReputation() {
    if (!g_published) return;
    host::WithdrawGameService(SC_REPUTATION_SERVICE_NAME);
    g_reputation.Stop();
    g_e = {};
    g_published = false;
}

void ReleaseReputationOwner(const void* owner) { g_reputation.ReleaseOwner(owner); }

}  // namespace sco::game::services
