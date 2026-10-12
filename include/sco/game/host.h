#pragma once
// Star Citizen OFFLINE host-source composition, never a plugin service or patch authority.
// Requires SCO_GAME_SC=ON; link sco_game_host. The shell supplies OS admission evidence,
// paths, logging, window/message and renderer adapters. No OS policy is inferred here.
#include "sco/app.h"
#include <cstdint>

namespace sco::game::host {
enum class Session : uint32_t { Unknown, Offline, Online };
// Clear means positively checked: correct target, no anti-cheat, blocked official network.
// A failed OS inspection MUST yield Unknown, never Clear. Present always refuses.
enum class Check : uint32_t { Unknown, Clear, Present };
struct Admission {
    Session session = Session::Unknown;
    Check antiCheat = Check::Unknown;
    Check officialNetwork = Check::Unknown;
    Check targetProcess = Check::Unknown;
};
enum class Phase : uint32_t { Idle, Preparing, Prepared, Starting, Running, Stopping, Stopped, Failed };
struct Snapshot {
    Phase phase = Phase::Idle;
    bool signaturesResolved = false;
};
struct Options {
    uint32_t size = sizeof(Options); // C++ source contract, not frozen sco_api or a wire ABI
    Admission (*inspect)() = nullptr; // synchronous OS adapter; exceptions refuse admission
    const Image* image = nullptr;    // readable immutable original image through Prepare
    app::Platform platform;          // image/registerSignatures MUST be null: owned here
};
// Serialized host entries, outside loader lock/runtime callouts. Prepare may precede the game
// thread; Start binds the game thread via app::Start. Unknown/online/EAC/unblocked-network/wrong
// target refuses BEFORE registration/resolution, hooks, service publication or plugin callbacks.
// All rows resolve before any later engine mutation. Missing rows remain unavailable, not guessed.
// Options are copied; pointed-to Platform inputs retain app.h's lifetime contract.
Result Prepare(const Options& options);
// Re-inspects admission before any publication. Prepared only; no feature fallback tick.
Result Start();
// Game thread. Stop cancels tasks through app::Stop; repeated stop is safe. No restart from
// Stopped/Failed: process-lifetime signature/hook ownership requires a fresh host process.
Result Stop();
// Immutable scalar observation; no provider/table callbacks. May read on any thread.
Snapshot Status();
} // namespace sco::game::host
