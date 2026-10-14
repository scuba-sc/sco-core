// SPDX-License-Identifier: GPL-3.0-only
// game.contracts 1.0 on Windows: the ContractsProvider built from rows, and the published table.
// The read is the one docs/game/contracts.md and include/sco/game/contracts.h document:
// contracts.mission_settings is the global the row's pattern loads with "mov rax, [rip+X]" (RIP
// at +3), and that pattern's "vmovss xmm1, [rax+0x174]" pins kStreamRadius, the float sc-offline
// writes (the contracts.stream_radius capability). Reads only: no hooks, no patches, no writes.
// The capability stays OFF (ContractsProvider::probed is false) until an in-game run confirms it.
#include "contracts_service.h"
#include "../../api/internal.h"
#include "sco/game/contracts.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

constexpr const char* kRadiusRows[] = { "contracts.mission_settings" };

struct Engine {
    const uintptr_t* missionSettings = nullptr;
};
Engine    g_e;
Contracts g_contracts;
bool      g_published = false;

bool RowsOk(const char* const* ids, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const SigResult* s = SigLookup(ids[i]);
        if (!s || s->state != SigState::Ok) return false;
    }
    return true;
}

// ---- game read, under SEH ----------------------------------------------------------------------
//
// 1 read, 0 not there, -1 the game faulted. No C++ object with a destructor lives in this frame.

int ReadStreamRadius(float* radius) {
    *radius = 0.0f;
    __try {
        const uintptr_t settings = *g_e.missionSettings;
        if (!settings) return 0;
        *radius = *reinterpret_cast<const float*>(settings + sco::game::contracts::kStreamRadius);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *radius = 0.0f;
        return -1;
    }
}

// ---- the provider ------------------------------------------------------------------------------

sco_result StreamRadius(float* radius, const char** why) {
    const int got = ReadStreamRadius(radius);
    if (got < 0) { *why = "the game faulted reading the mission settings"; return SCO_FAILED; }
    if (!got) { *why = "the mission settings aren't loaded yet"; return SCO_NOT_FOUND; }
    return SCO_OK;
}

bool OnThread() { return OnGameThread(); }
bool Live(const sco_plugin* p) { return host::PluginId(p) && !detail::Released(p); }

void LogCall(const sco_plugin* self, const char* fn, sco_result r, const char* why) {
    const char* id = self ? host::PluginId(self) : nullptr;
    Log("[game] contracts: %s: %s -> %s: %s", id ? id : "?", fn, ResultName(static_cast<Result>(r)), why);
}

sco_result SvcStreamRadius(sco_plugin* self, float* outRadius) { return g_contracts.StreamRadius(self, outRadius); }
sco_result SvcLastError(sco_plugin* self, char* out, uint32_t* inoutSize) { return g_contracts.LastError(self, out, inoutSize); }

const sc_contracts_v1 kService = {
    sizeof(sc_contracts_v1), 0, SvcStreamRadius, SvcLastError,
};

}  // namespace

Result StartContracts() {
    if (g_published) return Result::Ok;
    if (!OnGameThread()) return Result::WrongThread;
    ContractsProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    p.radius_rows = RowsOk(kRadiusRows, std::size(kRadiusRows));
    p.probed = false;
    g_e = {};
    if (p.radius_rows) g_e.missionSettings = reinterpret_cast<const uintptr_t*>(Sig("contracts.mission_settings"));
    p.stream_radius = StreamRadius;
    p.log = LogCall;
    if (g_contracts.Start(p) != SCO_OK) {
        g_e = {};
        return Result::WrongThread;
    }
    const Result r = host::ProvideGameService(SC_CONTRACTS_SERVICE_NAME, SC_CONTRACTS_SERVICE_VERSION, &kService);
    if (r != Result::Ok) {
        g_contracts.Stop();
        g_e = {};
        return r;
    }
    g_published = true;
    Log("[game] game.contracts 1.0 published: game.contracts.stream_radius held OFF pending an in-game run");
    return Result::Ok;
}

void StopContracts() {
    if (!g_published) return;
    host::WithdrawGameService(SC_CONTRACTS_SERVICE_NAME);
    g_contracts.Stop();
    g_e = {};
    g_published = false;
}

void ReleaseContractsOwner(const void* owner) { g_contracts.ReleaseOwner(owner); }

}  // namespace sco::game::services
