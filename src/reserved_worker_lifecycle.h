// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

namespace sco {

#if defined(SCO_RESERVED_SESSION_TESTING) || defined(SCO_RESERVED_WORKER_TESTING)
namespace reserved_worker_testing {
inline std::atomic<void (*)(void*) noexcept> beforePublish{nullptr};
inline std::atomic<void*> beforePublishContext{nullptr};
inline std::atomic<bool> failNextLaunch{false};
inline void SetBeforePublish(void (*hook)(void*) noexcept, void* context) noexcept {
    beforePublishContext.store(context);
    beforePublish.store(hook);
}
}
#endif

// Private terminal worker group. The caller transfers a uniquely owned capsule
// (not a borrowed pointer or no-op ownership token). State and capsule remain
// rooted for process lifetime, on success and failure. The containing module
// must also remain mapped. Construct the capsule and this group outside broker
// and lifecycle locks. The capsule must own all worker-accessed backing.
//
// Acquire broker admission before reserve_start(), and hold it until after the
// Start and all callable construction temporaries have been destroyed. Close
// broker admission before retire_until(), and drain all admitted operations
// before committing the enclosing session. Keep this object alive during every
// method call. Never run user code or destroy user objects under broker locks.
class ReservedWorkerLifecycle {
    struct Slot {
        std::thread thread;
        std::thread::id starter;
        std::thread::id worker;
        bool pending = false;
    };
    struct State {
        explicit State(std::shared_ptr<void> owned_capsule) noexcept
            : capsule(std::move(owned_capsule)) {}
        // The intentionally leaked State is the process root for the capsule.
        const std::shared_ptr<void> capsule;
        State* next = nullptr;
        std::mutex mutex;
        std::condition_variable cv;
        std::array<Slot, 64> slots{};
        std::thread joiner;
        size_t used = 0;
        size_t pending = 0;
        std::atomic<bool> stop{false};
        bool closed = false;
        bool failed = false;
        bool join_done = false;
    };
public:
    static constexpr size_t MaxWorkers = 64;
    using Clock = std::chrono::steady_clock;
    enum class Result { Joined, FailedRetained, SelfJoinRefused, Unavailable };

    class Start {
    public:
        Start(const Start&) = delete;
        Start& operator=(const Start&) = delete;
        Start(Start&&) = delete;
        Start& operator=(Start&&) = delete;
        ~Start() {
            if (!state_) return;
            std::lock_guard<std::mutex> lock(state_->mutex);
            slot_->pending = false;
            if (!launched_) {
                state_->failed = true;
                state_->stop.store(true, std::memory_order_release);
            }
            --state_->pending;
            state_->cv.notify_all();
        }
        explicit operator bool() const noexcept { return state_ != nullptr; }

        // Thread-bound. Construct f only after checking this reservation; keep
        // it alive until all f construction temporaries have been destroyed.
        // Call at most once. The worker receives a stop flag. A throwing worker,
        // abandoned reservation or failed launch faults the group. User
        // destructors must not throw. No user code runs under this mutex.
        template<class F> bool launch(F&& f) noexcept {
            if (!state_ || launched_) return false;
            launched_ = true;
            State* const state = state_;
            Slot* const slot = slot_;
            try {
#if defined(SCO_RESERVED_SESSION_TESTING) || defined(SCO_RESERVED_WORKER_TESTING)
                if (reserved_worker_testing::failNextLaunch.exchange(false)) throw std::bad_alloc();
#endif
                std::thread worker([state, slot, fn = std::decay_t<F>(std::forward<F>(f))]() mutable {
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        slot->worker = std::this_thread::get_id();
                    }
                    try { std::invoke(fn, std::as_const(state->stop)); }
                    catch (...) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->failed = true;
                        state->stop.store(true, std::memory_order_release);
                        state->cv.notify_all();
                    }
                });
#if defined(SCO_RESERVED_SESSION_TESTING) || defined(SCO_RESERVED_WORKER_TESTING)
                // No lifecycle lock is held and the test hook cannot throw.
                if (auto hook = reserved_worker_testing::beforePublish.load())
                    hook(reserved_worker_testing::beforePublishContext.load());
#endif
                // The reservation exclusively owns this unused slot. Publish
                // without locking: retirement cannot inspect worker handles
                // until pending reaches zero under the mutex in ~Start().
                slot->thread = std::move(worker);
                return true;
            } catch (...) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->failed = true;
                state->stop.store(true, std::memory_order_release);
                state->cv.notify_all();
                return false;
            }
        }
    private:
        friend class ReservedWorkerLifecycle;
        Start(State* state, Slot* slot) noexcept : state_(state), slot_(slot) {}
        State* state_;
        Slot* slot_;
        bool launched_ = false;
    };

    // The ordinary default-deleter unique_ptr transfers actual ownership into
    // an internally created shared_ptr control block. All allocation and any
    // cleanup on failure occur outside lifecycle and broker locks.
    template<class T> explicit ReservedWorkerLifecycle(std::unique_ptr<T> capsule)
        : state_(new State(std::shared_ptr<void>(std::move(capsule)))) {
        static_assert(!std::is_array_v<T> && !std::is_void_v<T>);
        auto& root = retained_root();
        State* head = root.load(std::memory_order_relaxed);
        do { state_->next = head; }
        while (!root.compare_exchange_weak(head, state_, std::memory_order_release,
                                           std::memory_order_relaxed));
    }
    ReservedWorkerLifecycle(const ReservedWorkerLifecycle&) = delete;
    ReservedWorkerLifecycle& operator=(const ReservedWorkerLifecycle&) = delete;
    ~ReservedWorkerLifecycle() = default;

    // At most MaxWorkers starts per group, including abandoned reservations.
    // Exhaustion faults and closes worker admission; it never silently omits a
    // requested worker. No storage is allocated on this path.
    [[nodiscard]] Start reserve_start() noexcept {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->closed || state_->failed) return Start(nullptr, nullptr);
        if (state_->used == MaxWorkers) {
            state_->failed = true;
            state_->stop.store(true, std::memory_order_release);
            state_->cv.notify_all();
            return Start(nullptr, nullptr);
        }
        auto* slot = &state_->slots[state_->used++];
        slot->starter = std::this_thread::get_id();
        slot->pending = true;
        ++state_->pending;
        return Start(state_, slot);
    }

    bool wait_until_closed(Clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(state_->mutex);
        return state_->cv.wait_until(lock, deadline, [this] { return state_->closed; });
    }

    // One retirement attempt. Deadline bounds condition waits and final commit,
    // not scheduling, mutex acquisition, or join of the signaled joiner. The
    // joiner runs no supplied callable. Self-join is refused before closure;
    // thread-id reuse may conservatively refuse a later retirement.
    Result retire_until(Clock::time_point deadline) {
        State* const state = state_;
        std::unique_lock<std::mutex> lock(state->mutex);
        if (state->closed) return Result::Unavailable;
        const auto self = std::this_thread::get_id();
        for (size_t i = 0; i != state->used; ++i) {
            const auto& slot = state->slots[i];
            if (slot.worker == self || (slot.starter == self && slot.pending))
                return Result::SelfJoinRefused;
        }
        state->closed = true;
        state->stop.store(true, std::memory_order_release);
        state->cv.notify_all();
        if (!state->cv.wait_until(lock, deadline, [state] { return state->pending == 0; }) ||
            Clock::now() >= deadline) {
            state->failed = true;
            return Result::FailedRetained;
        }
        lock.unlock();
        // Thread creation and destruction are deliberately outside the mutex,
        // including allocation failure and user-provided allocation handlers.
        try {
            state->joiner = std::thread([state] {
                bool failed = false;
                for (size_t i = 0; i != state->used; ++i) {
                    auto& thread = state->slots[i].thread;
                    if (!thread.joinable()) continue;
                    try { thread.join(); } catch (...) { failed = true; }
                }
                std::lock_guard<std::mutex> guard(state->mutex);
                state->failed = state->failed || failed;
                state->join_done = true;
                state->cv.notify_all();
            });
        } catch (...) {
            lock.lock();
            state->failed = true;
            return Result::FailedRetained;
        }
        lock.lock();
        const bool completed = state->cv.wait_until(lock, deadline, [state] { return state->join_done; });
        if (!completed) state->failed = true;
        lock.unlock();
        if (!completed) {
            // The joiner handle resides in the permanently retained state. If
            // detach fails it remains joinable without being destroyed.
            try { state->joiner.detach(); } catch (...) {}
            return Result::FailedRetained;
        }
        // This includes the internal joiner's TLS destruction and occurs
        // outside the lifecycle lock.
        try { state->joiner.join(); }
        catch (...) {
            // Preserve a still-joinable handle in retained state.
            lock.lock();
            state->failed = true;
            return Result::FailedRetained;
        }
        lock.lock();
        if (Clock::now() >= deadline) state->failed = true;
        return state->failed ? Result::FailedRetained : Result::Joined;
    }
private:
    static std::atomic<State*>& retained_root() noexcept {
        static std::atomic<State*> root{nullptr};
        return root;
    }
    State* const state_;
};
} // namespace sco
