// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace sco {
// Host-source ownership contract, NOT a native target execution barrier. Every
// cached read must hold Read for its entire use; every worker must be owned here.
// Arbitrary plugins/native targets cannot be retrofitted by returning true from a
// callback. Keep this object and all borrowed backing live after a failed drain.
class OwnedShutdown {
    struct Worker { std::thread thread; bool finished = false; };
public:
    class Read {
    public:
        Read() = default;
        Read(const Read&) = delete;
        Read& operator=(const Read&) = delete;
        Read(Read&& r) noexcept : owner_(std::exchange(r.owner_, nullptr)) {}
        ~Read() {
            if (!owner_) return;
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            --owner_->readers_;
            owner_->cv_.notify_all();
        }
        explicit operator bool() const noexcept { return owner_ != nullptr; }
    private:
        friend class OwnedShutdown;
        explicit Read(OwnedShutdown* p) : owner_(p) {}
        OwnedShutdown* owner_ = nullptr;
    };
    OwnedShutdown() = default;
    OwnedShutdown(const OwnedShutdown&) = delete;
    OwnedShutdown& operator=(const OwnedShutdown&) = delete;
    // Destruction requires no concurrent public calls, starts or Read leases.
    // It joins, never detaches or terminates. A timed-out owner must retain this
    // and borrowed backing until all starts, workers and callers have ended;
    // Timed waits can refuse a stuck worker, but the overall call has no hard
    // wall-clock bound: mutex acquisition and thread creation can be delayed.
    ~OwnedShutdown() {
        stop_.store(true);
        if (joiner_.joinable()) joiner_.join();
        for (auto& w : workers_) if (w->thread.joinable()) w->thread.join();
    }
    Read try_read() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return {};
        ++readers_;
        return Read(this);
    }
    template<class Work> bool start_worker(Work&& work) {
        // User-defined callable construction and std::thread construction can
        // block or re-enter this domain. Neither may run under mutex_. Reserve
        // an in-flight start first so a concurrent drain cannot join too early.
        std::unique_ptr<Worker> worker;
        try { worker = std::make_unique<Worker>(); } catch (...) { return false; }
        Worker* p = worker.get();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || workers_.size() == 64) return false;
            try { workers_.push_back(std::move(worker)); } catch (...) { return false; }
            ++starting_;
        }
        try {
            auto fn = std::make_shared<std::decay_t<Work>>(std::forward<Work>(work));
            bool canceled;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                canceled = closed_;
            }
            if (canceled) {
                // Destructors can also execute user code. Keep the reservation
                // until it has completed, without holding the mutex.
                fn.reset();
                std::lock_guard<std::mutex> lock(mutex_);
                p->finished = true;
                --starting_;
                cv_.notify_all();
                return false;
            }
            std::thread thread([this, p, fn]() mutable {
                bool failed = false;
                try { (*fn)([this] { return stop_.load(); }); } catch (...) { failed = true; }
                std::lock_guard<std::mutex> done(mutex_);
                faulted_ = faulted_ || failed;
                if (failed) { closed_ = true; stop_.store(true); }
                p->finished = true;
                cv_.notify_all();
            });
            // The thread or its join owns the last callable reference before
            // the start reservation is released.
            fn.reset();
            std::lock_guard<std::mutex> lock(mutex_);
            p->thread = std::move(thread);
            --starting_;
            cv_.notify_all();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            // The slot has no thread and will be ignored by the joiner. A
            // failed start is terminal: callers cannot assume an incomplete
            // worker census is safe to retire.
            p->finished = true;
            --starting_;
            faulted_ = closed_ = true;
            stop_.store(true);
            cv_.notify_all();
            return false;
        }
        return true;
    }
    bool drain_until(std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (attempted_) return false;
        attempted_ = closed_ = true;
        stop_.store(true);
        const auto done = [&] {
            if (readers_ || starting_) return false;
            for (const auto& w : workers_) if (!w->finished) return false;
            return true;
        };
        if (!cv_.wait_until(lock, deadline, done)) { faulted_ = true; return false; }
        // Joining on this control thread is not bounded (thread-local destructors
        // may still run after finished). An owned joiner supplies actual join proof.
        try {
            joiner_ = std::thread([this] {
                for (auto& w : workers_) if (w->thread.joinable()) w->thread.join();
                std::lock_guard<std::mutex> joined(mutex_);
                joined_ = true;
                cv_.notify_all();
            });
        } catch (...) { faulted_ = true; return false; }
        if (!cv_.wait_until(lock, deadline, [&] { return joined_; })) {
            faulted_ = true; return false;
        }
        ready_ = !faulted_ && std::chrono::steady_clock::now() <= deadline;
        return ready_;
    }
    bool ready() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ready_ && !faulted_;
    }
    bool open() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !closed_;
    }
    void fault() {
        std::lock_guard<std::mutex> lock(mutex_);
        faulted_ = closed_ = true;
        ready_ = false;
        stop_.store(true);
        cv_.notify_all();
    }
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    bool closed_ = false, attempted_ = false, ready_ = false, faulted_ = false;
    size_t readers_ = 0;
    size_t starting_ = 0;
    bool joined_ = false;
    std::thread joiner_;
    std::vector<std::unique_ptr<Worker>> workers_;
};
} // namespace sco
