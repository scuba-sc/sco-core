// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// game.hangar 1.0 (sc_hangar.h) inside the game pack. Hangar is the portable policy: thread, self
// and argument checks, the capability gate, last_error. The engine calls come through a
// HangarProvider; hangar_engine.cpp builds it from rows on Windows and publishes the table
// (StartHangar / StopHangar, from services::Start / Stop in spatial.cpp).
#include <sc_hangar.h>
#include "sco/runtime.h"
#include <cstddef>
#include <cstdint>

namespace sco::game::services {

inline constexpr char kHangarCapLift[]      = "game.hangar.lift";
inline constexpr char kHangarCapAtcCancel[] = "game.hangar.atc_cancel";

// Host-only seam, copied at Start; never exposed to plugins. Callbacks are synchronous, run on
// the game thread, and return a static reason in *why when they fail.
struct HangarProvider {
    bool (*on_thread)() = nullptr;
    bool (*live)(const sco_plugin*) = nullptr;
    bool lift_rows = false;   // every row game.hangar.lift reads is OK on this build
    bool atc_rows = false;    // every row game.hangar.atc_cancel reads is OK
    // Set only by an adapter whose engine behaviour an in-game run has confirmed. No production
    // adapter sets it, so both capabilities stay OFF (docs/game/status.md).
    bool probed = false;
    sco_result (*lift_of_area)(uint64_t area, uint64_t* lift, const char** why) = nullptr;
    sco_result (*lift_request)(uint64_t lift, uint32_t op, const char** why) = nullptr;
    sco_result (*atc_cancel)(uint64_t atc, const char** why) = nullptr;
    void (*log)(const sco_plugin*, const char* fn, sco_result, const char* why) = nullptr;
};

class Hangar {
public:
    static constexpr size_t kErrMax = 160;
    static constexpr size_t kErrSlots = 64;   // plugin handles aren't reused; past it the oldest goes

    // Sets both capabilities (OFF unless rows are OK and the provider is probed). Game thread.
    sco_result Start(const HangarProvider& provider);
    void Stop();
    bool Started() const { return started_; }

    sco_result LiftOfLandingArea(sco_plugin* self, uint64_t area, uint64_t* outLift);
    sco_result LiftRequest(sco_plugin* self, uint64_t lift, uint32_t op);
    sco_result AtcCancelRequest(sco_plugin* self, uint64_t atc);
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

    HangarProvider p_{};
    bool started_ = false, lift_ = false, atc_ = false;
    char liftWhy_[kErrMax] = "", atcWhy_[kErrMax] = "";
    ErrSlot errs_[kErrSlots] = {};
    uint64_t seq_ = 0;
};

// Windows game pack only (hangar_engine.cpp). Publishes game.hangar 1.0; BadArg when the name is
// taken, nothing left published then. Game thread.
Result StartHangar();
void   StopHangar();
// spawn.cpp's release hook: the owner's last error is forgotten.
void   ReleaseHangarOwner(const void* owner);

}  // namespace sco::game::services
