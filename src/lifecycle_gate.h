// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace sco {

// Cooperative only: every relevant entry path must acquire a lease. This does
// not exclude unmanaged native threads or prove Windows instruction patch safety.
// The gate must outlive all calls and leases; reader leases stay on their caller.
class LifecycleGate {
public:
    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : gate_(std::exchange(other.gate_, nullptr)) {}
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) { reset(); gate_ = std::exchange(other.gate_, nullptr); }
            return *this;
        }
        ~Lease() { reset(); }
        explicit operator bool() const noexcept { return gate_ != nullptr; }
    private:
        friend class LifecycleGate;
        explicit Lease(LifecycleGate* gate) : gate_(gate) {}
        void reset() noexcept {
            if (!gate_) return;
            std::lock_guard lock(gate_->mutex_);
            --gate_->readers_;
            gate_->cv_.notify_all();
            gate_ = nullptr;
        }
        LifecycleGate* gate_ = nullptr;
    };

    // A cleanup permit is a thread-bound synchronous check, not an async lease.
    // It cannot be copied or moved out of the cleanup call.
    class CleanupPermit {
    public:
        CleanupPermit(const CleanupPermit&) = delete;
        CleanupPermit& operator=(const CleanupPermit&) = delete;
        explicit operator bool() const {
            std::lock_guard lock(gate_->mutex_);
            return gate_->state_ == State::cleanup && gate_->closer_ == std::this_thread::get_id();
        }
    private:
        friend class LifecycleGate;
        explicit CleanupPermit(LifecycleGate* gate) : gate_(gate) {}
        LifecycleGate* gate_;
    };

    Lease try_enter() {
        std::lock_guard lock(mutex_);
        if (state_ != State::open) return {};
        ++readers_;
        return Lease(this);
    }

    CleanupPermit try_cleanup_enter() {
        return CleanupPermit(this);
    }

    // Observes admission closure, not completion or native quiescence.
    bool wait_until_closed(std::chrono::steady_clock::time_point deadline) {
        std::unique_lock lock(mutex_);
        return cv_.wait_until(lock, deadline, [&] { return state_ != State::open; });
    }

    // Terminal even on timeout, failure, exception, or concurrent fault.
    template<class Cleanup>
    bool close_until(std::chrono::steady_clock::time_point deadline, Cleanup&& cleanup) {
        std::unique_lock lock(mutex_);
        if (state_ != State::open) return false;
        state_ = State::draining;
        closer_ = std::this_thread::get_id();
        cv_.notify_all();
        if (!cv_.wait_until(lock, deadline, [&] { return readers_ == 0 || state_ == State::faulted; }) ||
            state_ == State::faulted) {
            state_ = State::faulted;
            closer_ = {};
            cv_.notify_all();
            return false;
        }
        state_ = State::cleanup;
        lock.unlock();
        bool result;
        try {
            result = static_cast<bool>(std::forward<Cleanup>(cleanup)());
        } catch (...) {
            lock.lock();
            state_ = State::faulted;
            closer_ = {};
            cv_.notify_all();
            throw;
        }
        lock.lock();
        result = result && state_ == State::cleanup;
        state_ = result ? State::closed : State::faulted;
        closer_ = {};
        cv_.notify_all();
        return result;
    }

    void fault() {
        std::lock_guard lock(mutex_);
        state_ = State::faulted;
        closer_ = {};
        cv_.notify_all();
    }

private:
    enum class State { open, draining, cleanup, closed, faulted };
    std::mutex mutex_;
    std::condition_variable cv_;
    State state_ = State::open;
    std::size_t readers_ = 0;
    std::thread::id closer_;
};
} // namespace sco
