// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// game.reputation 1.0 (sc_reputation.h) inside the game pack. Reputation is the portable policy:
// thread, self and argument checks, the capability gate, last_error. The engine reads come through
// a ReputationProvider; reputation_engine.cpp builds it from rows on Windows and publishes the
// table (StartReputation / StopReputation, from services::Start / Stop in spatial.cpp).
#include <sc_reputation.h>
#include "sco/runtime.h"
#include <cstddef>
#include <cstdint>

namespace sco::game::services {

inline constexpr char kReputationCapChecks[] = "game.reputation.checks";

// Host-only seam, copied at Start; never exposed to plugins. Callbacks are synchronous, run on
// the game thread, and return a static reason in *why when they fail.
struct ReputationProvider {
    bool (*on_thread)() = nullptr;
    bool (*live)(const sco_plugin*) = nullptr;
    bool rows = false;     // every row game.reputation.checks reads is OK on this build
    // Set only by an adapter whose engine behaviour an in-game run has confirmed. No production
    // adapter sets it, so the capability stays OFF (docs/game/status.md).
    bool probed = false;
    // Fills sites, intact and services; the policy owns size.
    sco_result (*checks)(sc_reputation_checks* out, const char** why) = nullptr;
    void (*log)(const sco_plugin*, const char* fn, sco_result, const char* why) = nullptr;
};

class Reputation {
public:
    static constexpr size_t kErrMax = 160;
    static constexpr size_t kErrSlots = 64;   // plugin handles aren't reused; past it the oldest goes

    // Sets the capability (OFF unless rows are OK and the provider is probed). Game thread.
    sco_result Start(const ReputationProvider& provider);
    void Stop();
    bool Started() const { return started_; }

    sco_result Checks(sco_plugin* self, sc_reputation_checks* out);
    sco_result LastError(sco_plugin* self, char* out, uint32_t* inoutSize);
    // A plugin unloaded or crashed: its last error is forgotten.
    void ReleaseOwner(const void* owner);

private:
    struct ErrSlot { const void* owner; uint64_t seq; char msg[kErrMax]; };
    sco_result Admit(sco_plugin* self, const char* fn);
    sco_result Fail(sco_plugin* self, const char* fn, sco_result r, const char* why);
    ErrSlot& SlotFor(const void* owner);
    const ErrSlot* FindSlot(const void* owner) const;

    ReputationProvider p_{};
    bool started_ = false, checks_ = false;
    char checksWhy_[kErrMax] = "";
    ErrSlot errs_[kErrSlots] = {};
    uint64_t seq_ = 0;
};

// Windows game pack only (reputation_engine.cpp). Publishes game.reputation 1.0; BadArg when the
// name is taken, nothing left published then. Game thread.
Result StartReputation();
void   StopReputation();
// spawn.cpp's release hook: the owner's last error is forgotten.
void   ReleaseReputationOwner(const void* owner);

}  // namespace sco::game::services
