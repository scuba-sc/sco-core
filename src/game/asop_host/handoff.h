// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>

namespace sco::game::asop_host {

// Internal game-pack continuation handoff; never part of the plugin API.
// Run is synchronous: callback/context may point into the caller's stack. The
// take deadline limits only the time spent queued, never a callback's lifetime.
// The host supplies inlineNow for its main-thread / visible-here / disabled path.
// Drain is called by the host's main-thread pump; recursive callbacks run inline.
// Shutdown closes admission and wakes untaken callers to run their own fallback.
// It is not a join: keep this object alive until every Run and Drain has returned.
class Handoff final {
public:
    using Callback = void (*)(void*);
    Handoff() = default;
    Handoff(const Handoff&) = delete;
    Handoff& operator=(const Handoff&) = delete;

    // callback must be non-null. C++ exceptions from a drained callback are
    // delivered back to its Run caller after completion (no frame is stranded).
    void Run(Callback callback, void* context, bool inlineNow,
             std::chrono::milliseconds deadline = std::chrono::milliseconds{3000});
    void Drain();
    void Shutdown();
    // Untaken queued calls only; callbacks already taken by Drain are excluded.
    std::size_t Pending() const;

private:
    struct Call {
        Callback callback;
        void* context;
        bool taken = false;
        bool done = false;
        std::exception_ptr error;
    };
    static constexpr std::size_t kCapacity = 32;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::array<Call*, kCapacity> pending_{};
    std::size_t count_ = 0;
    bool stopped_ = false;
};

} // namespace sco::game::asop_host
