// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// game.contracts 1.0 (sc_contracts.h) inside the game pack. Contracts is the portable policy:
// thread, self and argument checks, the capability gate, last_error. The engine read comes
// through a ContractsProvider; contracts_engine.cpp builds it from rows on Windows and publishes
// the table (StartContracts / StopContracts, from services::Start / Stop in spatial.cpp). Named
// contracts_service.h so it never shadows include/sco/game/contracts.h.
#include <sc_contracts.h>
#include "sco/runtime.h"
#include <cstddef>
#include <cstdint>

namespace sco::game::services {

inline constexpr char kContractsCapStreamRadius[] = "game.contracts.stream_radius";

// Host-only seam, copied at Start; never exposed to plugins. Callbacks are synchronous, run on
// the game thread, and return a static reason in *why when they fail.
struct ContractsProvider {
    bool (*on_thread)() = nullptr;
    bool (*live)(const sco_plugin*) = nullptr;
    bool radius_rows = false;   // every row game.contracts.stream_radius reads is OK on this build
    // Set only by an adapter whose engine behaviour an in-game run has confirmed. No production
    // adapter sets it, so the capability stays OFF (docs/game/status.md).
    bool probed = false;
    sco_result (*stream_radius)(float* radius, const char** why) = nullptr;
    void (*log)(const sco_plugin*, const char* fn, sco_result, const char* why) = nullptr;
};

class Contracts {
public:
    static constexpr size_t kErrMax = 160;
    static constexpr size_t kErrSlots = 64;   // plugin handles aren't reused; past it the oldest goes

    // Sets the capability (OFF unless rows are OK and the provider is probed). Game thread.
    sco_result Start(const ContractsProvider& provider);
    void Stop();
    bool Started() const { return started_; }

    sco_result StreamRadius(sco_plugin* self, float* outRadius);
    sco_result LastError(sco_plugin* self, char* out, uint32_t* inoutSize);
    // A plugin unloaded or crashed: its last error is forgotten.
    void ReleaseOwner(const void* owner);

private:
    struct ErrSlot { const void* owner; uint64_t seq; char msg[kErrMax]; };
    sco_result Admit(sco_plugin* self, const char* fn);
    sco_result Gate(sco_plugin* self, const char* fn, bool on, const char* why);
    sco_result Fail(sco_plugin* self, const char* fn, sco_result r, const char* why);
    ErrSlot& SlotFor(const void* owner);
    const ErrSlot* FindSlot(const void* owner) const;

    ContractsProvider p_{};
    bool started_ = false, radius_ = false;
    char radiusWhy_[kErrMax] = "";
    ErrSlot errs_[kErrSlots] = {};
    uint64_t seq_ = 0;
};

// Windows game pack only (contracts_engine.cpp). Publishes game.contracts 1.0; BadArg when the
// name is taken, nothing left published then. Game thread.
Result StartContracts();
void   StopContracts();
// spawn.cpp's release hook: the owner's last error is forgotten.
void   ReleaseContractsOwner(const void* owner);

}  // namespace sco::game::services
