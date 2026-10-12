// SPDX-License-Identifier: GPL-3.0-only
// Project-authored portable fixture; include at file scope after defining CHECK.
#include "handoff.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace handoff_test {
using sco::game::asop_host::Handoff;
using namespace std::chrono_literals;

// Failure must not unwind past a joinable worker or leave a stack callback dangling.
inline void Require(bool ok) {
    if (!ok) {
        std::fprintf(stderr, "FAIL handoff synchronization deadline\n");
        std::abort();
    }
}

struct Signal {
    std::mutex mutex;
    std::condition_variable changed;
    bool set = false;
    void Set() {
        std::lock_guard lock(mutex);
        set = true;
        changed.notify_all();
    }
    bool Wait(std::chrono::milliseconds timeout = 5s) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, timeout, [this] { return set; });
    }
};

inline void WaitPending(Handoff& handoff, std::size_t count) {
    const auto limit = std::chrono::steady_clock::now() + 5s;
    while (handoff.Pending() != count) {
        Require(std::chrono::steady_clock::now() < limit);
        std::this_thread::yield();
    }
}

struct Blocked {
    Signal entered, release, returned;
    std::atomic<bool> alive{false}, destroyed{false}, completed{false};
    std::thread::id callbackThread;
};

// The context itself is on the caller's stack, not just a heap-backed test object.
struct StackContext {
    Blocked& state;
    int value = 17;
    explicit StackContext(Blocked& s) : state(s) { state.alive = true; }
    ~StackContext() { state.alive = false; state.destroyed = true; }
};

inline void Block(void* opaque) {
    auto& context = *static_cast<StackContext*>(opaque);
    context.state.callbackThread = std::this_thread::get_id();
    context.state.entered.Set();
    Require(context.state.release.Wait());
    Require(context.state.alive && context.value == 17);
    context.value = 29;
    context.state.completed = true;
}

inline void Taken(bool shutdown) {
    Handoff handoff;
    Blocked state;
    std::thread caller([&] {
        {
            StackContext context(state);
            handoff.Run(Block, &context, false, 1s);
            Require(state.completed && context.value == 29);
        }
        state.returned.Set();
    });
    WaitPending(handoff, 1);
    std::thread consumer([&] { handoff.Drain(); });
    const auto consumerId = consumer.get_id();
    Require(state.entered.Wait());
    if (shutdown) handoff.Shutdown();
    CHECK(handoff.Pending() == 0);
    CHECK(!state.returned.Wait(1100ms)); // Exceeds the 1s take deadline after take.
    CHECK(state.alive && !state.destroyed && !state.completed);
    state.release.Set();
    caller.join();
    consumer.join();
    CHECK(state.destroyed && state.completed);
    CHECK(state.callbackThread == consumerId);
}

struct Count {
    int calls = 0;
    std::thread::id thread;
};
inline void Increment(void* opaque) {
    auto& count = *static_cast<Count*>(opaque);
    ++count.calls;
    count.thread = std::this_thread::get_id();
}
} // namespace handoff_test

static void TestHandoff() {
    using namespace handoff_test;
    // Explicit main-thread / visible-here path: no enqueue and synchronous return.
    {
        Handoff handoff;
        Count count;
        handoff.Run(Increment, &count, true);
        CHECK(count.calls == 1 && count.thread == std::this_thread::get_id());
        CHECK(handoff.Pending() == 0);
    }
    Taken(false); // Caller owns its stack until the consumer callback returns.
    Taken(true);  // Shutdown must not release a taken frame early.
    // Untaken take-deadline fallback executes on the original caller exactly once.
    {
        Handoff handoff;
        Count count;
        std::thread caller([&] { handoff.Run(Increment, &count, false, 10ms); });
        const auto callerId = caller.get_id();
        caller.join();
        handoff.Drain();
        CHECK(count.calls == 1 && count.thread == callerId);
        CHECK(handoff.Pending() == 0);
    }
    // A queued callback recursively calls Run(false): never queues behind itself.
    {
        Handoff handoff;
        struct Nested { Handoff& handoff; Count inner; std::thread::id outer; } nested{handoff, {}, {}};
        std::thread caller([&] {
            handoff.Run(+[](void* opaque) {
                auto& n = *static_cast<Nested*>(opaque);
                n.outer = std::this_thread::get_id();
                n.handoff.Run(Increment, &n.inner, false, 30s);
            }, &nested, false, 30s);
        });
        WaitPending(handoff, 1);
        handoff.Drain();
        caller.join();
        CHECK(nested.inner.calls == 1 && nested.inner.thread == nested.outer);
        CHECK(nested.outer == std::this_thread::get_id() && handoff.Pending() == 0);
    }
    // Shutdown wakes queued callers, and subsequent calls bypass the closed queue.
    {
        Handoff handoff;
        Count count;
        std::thread caller([&] { handoff.Run(Increment, &count, false, 30s); });
        const auto callerId = caller.get_id();
        WaitPending(handoff, 1);
        handoff.Shutdown();
        caller.join();
        handoff.Drain();
        CHECK(count.calls == 1 && count.thread == callerId);
        CHECK(handoff.Pending() == 0);
        Count after;
        handoff.Run(Increment, &after, false);
        CHECK(after.calls == 1 && after.thread == std::this_thread::get_id());
        handoff.Shutdown(); // Idempotent.
    }
    // The 33rd call falls back without waiting or stealing any of the 32 frames.
    {
        Handoff handoff;
        std::vector<Count> counts(32);
        std::vector<std::thread> callers;
        std::vector<std::thread::id> ids;
        for (std::size_t i = 0; i < counts.size(); ++i) {
            callers.emplace_back([&, i] { handoff.Run(Increment, &counts[i], false, 30s); });
            ids.push_back(callers.back().get_id());
            WaitPending(handoff, i + 1);
        }
        Count overflow;
        handoff.Run(Increment, &overflow, false, 30s);
        CHECK(overflow.calls == 1 && overflow.thread == std::this_thread::get_id());
        CHECK(handoff.Pending() == 32);
        handoff.Shutdown();
        for (auto& caller : callers) caller.join();
        for (std::size_t i = 0; i < counts.size(); ++i)
            CHECK(counts[i].calls == 1 && counts[i].thread == ids[i]);
        CHECK(handoff.Pending() == 0);
    }
}
