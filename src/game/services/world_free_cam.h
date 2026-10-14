// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// game.world 1.1's free_camera_flag (sc_world.h) inside the game pack. FreeCamFlag is the portable
// policy: the thread and argument checks and the capability gate. The read comes through a
// FreeCamFlagProvider; world.cpp builds it from the build.free_cam rows on Windows and records
// the refusals in game.world's shared last_error.
#include <sc_world.h>
#include <cstddef>
#include <cstdint>

namespace sco::game::services {

inline constexpr char kWorldCapFreeCamFlag[] = "game.world.free_camera_flag";

// Host-only seam, copied at Start; never exposed to plugins. read_flag is synchronous, runs on
// the game thread, and returns a static reason in *why when it fails.
struct FreeCamFlagProvider {
    bool (*on_thread)() = nullptr;
    bool rows = false;     // every row of the build.free_cam group is OK on this build
    // Set only by an adapter whose reading an in-game run has confirmed. No production adapter
    // sets it, so the capability stays OFF (docs/game/status.md).
    bool probed = false;
    sco_result (*read_flag)(uint32_t* set, const char** why) = nullptr;
};

class FreeCamFlag {
public:
    static constexpr size_t kWhyMax = 128;

    // Sets the capability: ON only when the rows are OK, the provider is probed and can read.
    void Start(const FreeCamFlagProvider& provider);
    void Stop();
    bool Ready() const { return started_ && on_; }

    // free_camera_flag: *outSet is 0 on every failure, and *why is the reason.
    sco_result Query(uint32_t* outSet, const char** why) const;

private:
    FreeCamFlagProvider p_{};
    bool started_ = false, on_ = false;
    char why_[kWhyMax] = "game.world is stopped";
};

}  // namespace sco::game::services
