// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// game.asop 1.0 (sc_asop.h) inside the game pack. Asop is the portable policy: thread, self and
// argument checks, the capability gate, last_error. The engine reads come through an AsopProvider;
// asop_engine.cpp builds it from rows on Windows and publishes the table (StartAsop / StopAsop,
// from services::Start / Stop in spatial.cpp). Named asop_service.h so it never shadows
// include/sco/game/asop.h.
#include <sc_asop.h>
#include "sco/runtime.h"
#include <cstddef>
#include <cstdint>

namespace sco::game::services {

inline constexpr char kAsopCapClaimTimeout[]   = "game.asop.claim_timeout";
inline constexpr char kAsopCapShardPersisted[] = "game.asop.shard_persisted";

// Host-only seam, copied at Start; never exposed to plugins. Callbacks are synchronous, run on
// the game thread, and return a static reason in *why when they fail.
struct AsopProvider {
    bool (*on_thread)() = nullptr;
    bool (*live)(const sco_plugin*) = nullptr;
    bool claim_rows = false;   // every row game.asop.claim_timeout reads is OK on this build
    bool shard_rows = false;   // every row game.asop.shard_persisted reads is OK
    // Set only by an adapter whose engine behaviour an in-game run has confirmed. No production
    // adapter sets it, so both capabilities stay OFF (docs/game/status.md).
    bool probed = false;
    sco_result (*claim_timeout)(int32_t* seconds, const char** why) = nullptr;
    sco_result (*shard_persisted)(uint32_t* persisted, const char** why) = nullptr;
    void (*log)(const sco_plugin*, const char* fn, sco_result, const char* why) = nullptr;
};

class Asop {
public:
    static constexpr size_t kErrMax = 160;
    static constexpr size_t kErrSlots = 64;   // plugin handles aren't reused; past it the oldest goes

    // Sets both capabilities (OFF unless rows are OK and the provider is probed). Game thread.
    sco_result Start(const AsopProvider& provider);
    void Stop();
    bool Started() const { return started_; }

    sco_result ClaimTimeout(sco_plugin* self, int32_t* outSeconds);
    sco_result ShardPersisted(sco_plugin* self, uint32_t* outPersisted);
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

    AsopProvider p_{};
    bool started_ = false, claim_ = false, shard_ = false;
    char claimWhy_[kErrMax] = "", shardWhy_[kErrMax] = "";
    ErrSlot errs_[kErrSlots] = {};
    uint64_t seq_ = 0;
};

// Windows game pack only (asop_engine.cpp). Publishes game.asop 1.0; BadArg when the name is
// taken, nothing left published then. Game thread.
Result StartAsop();
void   StopAsop();
// spawn.cpp's release hook: the owner's last error is forgotten.
void   ReleaseAsopOwner(const void* owner);

}  // namespace sco::game::services
