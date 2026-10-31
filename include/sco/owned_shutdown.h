// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <algorithm>
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
class OwnedShutdownHostAccess;
// Host-source ownership contract, NOT a native target execution barrier. Every
// cached read must hold Read for its entire use; every worker must be owned here.
// Arbitrary plugins/native targets cannot be retrofitted by returning true from a
// callback. Keep this object and all borrowed backing live after a failed drain.
class OwnedShutdown {
    friend class OwnedShutdownHostAccess;
    struct Worker {
        std::thread thread;
        bool finished = false, starting = true;
        std::thread::id starter;
    };
public:
    // Thread-bound host transition. Publication is linearized against close
    // and fault under the owner mutex; the lease remains counted until it is
    // released. No caller-provided code runs under that mutex.
    class Transition {
    public:
        Transition() = default;
        Transition(const Transition&) = delete;
        Transition& operator=(const Transition&) = delete;
        Transition(Transition&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
        ~Transition() {
            if (!owner_) return;
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            owner_->transitioning_ = false;
            owner_->transitionThread_ = {};
            owner_->cv_.notify_all();
        }
        explicit operator bool() const noexcept { return owner_ != nullptr; }
        bool valid_for(const OwnedShutdown& owner) const {
            if (owner_ != &owner) return false;
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            return !owner_->closed_ && !owner_->faulted_ && owner_->startup_ &&
                   owner_->transitioning_ && owner_->transitionThread_ == std::this_thread::get_id();
        }
        template<class T> bool publish(std::atomic<T>& phase, T expected, T desired) {
            if (!owner_) return false;
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if (owner_->closed_ || owner_->faulted_ ||
                owner_->transitionThread_ != std::this_thread::get_id()) return false;
            return phase.compare_exchange_strong(expected, desired);
        }
        template<class T> bool publish_and_open(std::atomic<T>& phase, T expected, T desired) {
            if (!owner_) return false;
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if (owner_->closed_ || owner_->faulted_ || !owner_->startup_ ||
                owner_->transitionThread_ != std::this_thread::get_id()) return false;
            if (!phase.compare_exchange_strong(expected, desired)) return false;
            owner_->startup_ = false;
            return true;
        }
    private:
        friend class OwnedShutdown;
        explicit Transition(OwnedShutdown* p) : owner_(p) {}
        OwnedShutdown* owner_ = nullptr;
    };
    // A read lease stays on the acquiring thread through destruction. Moving
    // it to another thread invalidates the domain and forces retained failure.
    class Read {
    public:
        Read() = default;
        Read(const Read&) = delete;
        Read& operator=(const Read&) = delete;
        Read(Read&& r) noexcept : owner_(std::exchange(r.owner_, nullptr)), thread_(r.thread_) {}
        ~Read() {
            if (!owner_) return;
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if (thread_ != std::this_thread::get_id()) {
                // Reads are thread-bound; transfer makes the proof invalid.
                owner_->faulted_ = owner_->closed_ = true;
                owner_->ready_ = false;
                owner_->stop_.store(true);
            }
            const auto it = std::find(owner_->readerThreads_.begin(), owner_->readerThreads_.end(), thread_);
            if (it != owner_->readerThreads_.end()) owner_->readerThreads_.erase(it);
            --owner_->readers_;
            owner_->cv_.notify_all();
        }
        explicit operator bool() const noexcept { return owner_ != nullptr; }
    private:
        friend class OwnedShutdown;
        explicit Read(OwnedShutdown* p) : owner_(p), thread_(std::this_thread::get_id()) {}
        OwnedShutdown* owner_ = nullptr;
        std::thread::id thread_;
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
        if (closed_ || startup_ || transitioning_) return {};
        try { readerThreads_.push_back(std::this_thread::get_id()); } catch (...) { return {}; }
        ++readers_;
        return Read(this);
    }
private:
    // Keep reader/worker admission shut between the distinct host startup
    // calls, not just while one call holds its transition permit.
    bool begin_startup() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || startup_ || transitioning_ || readers_ || starting_) return false;
        startup_ = true;
        return true;
    }
    Transition try_transition() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || transitioning_ || readers_ || starting_) return {};
        transitioning_ = true;
        transitionThread_ = std::this_thread::get_id();
        return Transition(this);
    }
    bool on_transition_thread() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !closed_ && !faulted_ && startup_ && transitioning_ &&
               transitionThread_ == std::this_thread::get_id();
    }
public:
    template<class Work> bool start_worker(Work&& work) {
        return start_worker_impl(std::forward<Work>(work), [](std::thread&) noexcept {});
    }
#ifdef SCO_OWNED_SHUTDOWN_TESTING
    // The standalone test can deterministically delay handle publication.
    template<class Work, class Hook> bool start_worker_with_launch_hook(Work&& work, Hook&& hook) {
        return start_worker_impl(std::forward<Work>(work), std::forward<Hook>(hook));
    }
#endif
private:
    template<class Work, class Hook> bool start_worker_impl(Work&& work, Hook&& hook) {
        static_assert(std::is_nothrow_invocable_v<Hook&, std::thread&>);
        // User-defined callable construction and std::thread construction can
        // block or re-enter this domain. Neither may run under mutex_. Reserve
        // an in-flight start first so a concurrent drain cannot join too early.
        std::unique_ptr<Worker> worker;
        try { worker = std::make_unique<Worker>(); } catch (...) { return false; }
        Worker* p = worker.get();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || startup_ || transitioning_ || workers_.size() == 64) return false;
            p->starter = std::this_thread::get_id();
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
                p->starting = false;
                --starting_;
                cv_.notify_all();
                return false;
            }
            std::thread thread([this, p, fn]() mutable {
                // This thread is dedicated to one owned worker. TLS remains
                // set through callable and thread-local destruction, without
                // confusing a later thread that reuses its OS thread id.
                currentWorker_ = this;
                bool failed = false;
                try { (*fn)([this] { return stop_.load(); }); } catch (...) { failed = true; }
                std::lock_guard<std::mutex> done(mutex_);
                faulted_ = faulted_ || failed;
                if (failed) { closed_ = true; stop_.store(true); }
                p->finished = true;
                cv_.notify_all();
            });
            hook(thread); // never under mutex_; production hook is a no-op
            // The thread or its join owns the last callable reference before
            // the start reservation is released.
            fn.reset();
            std::lock_guard<std::mutex> lock(mutex_);
            p->thread = std::move(thread);
            p->starting = false;
            --starting_;
            cv_.notify_all();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            // The slot has no thread and will be ignored by the joiner. A
            // failed start is terminal: callers cannot assume an incomplete
            // worker census is safe to retire.
            p->finished = true;
            p->starting = false;
            --starting_;
            faulted_ = closed_ = true;
            stop_.store(true);
            cv_.notify_all();
            return false;
        }
        return true;
    }
public:
    bool drain_until(std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (attempted_) return false;
        attempted_ = closed_ = true;
        stop_.store(true);
        cv_.notify_all();
        const auto current = std::this_thread::get_id();
        bool self = transitioning_ && transitionThread_ == current;
        self = self || std::find(readerThreads_.begin(), readerThreads_.end(), current) != readerThreads_.end();
        self = self || currentWorker_ == this;
        for (const auto& w : workers_)
            self = self || (w->starting && w->starter == current);
        if (self) {
            faulted_ = true;
            cv_.notify_all();
            return false;
        }
        const auto done = [&] {
            if (readers_ || starting_ || transitioning_) return false;
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
        return !closed_ && !startup_ && !transitioning_;
    }
    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }
    bool wait_until_closed(std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_until(lock, deadline, [&] { return closed_; });
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
    inline static thread_local OwnedShutdown* currentWorker_ = nullptr;
    bool closed_ = false, attempted_ = false, ready_ = false, faulted_ = false;
    size_t readers_ = 0;
    std::vector<std::thread::id> readerThreads_;
    size_t starting_ = 0;
    bool transitioning_ = false;
    bool startup_ = false;
    std::thread::id transitionThread_;
    bool joined_ = false;
    std::thread joiner_;
    std::vector<std::unique_ptr<Worker>> workers_;
};
} // namespace sco
