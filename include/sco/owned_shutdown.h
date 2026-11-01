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
class OwnedShutdownTestAccess;
class SyntheticOwnedSession;
// Host-source ownership contract, NOT a native target execution barrier. Every
// cached read must hold Read for its entire use; every worker must be owned here.
// Arbitrary plugins/native targets cannot be retrofitted by returning true from a
// callback. Keep this object and all borrowed backing live after a failed drain.
class OwnedShutdown {
    friend class OwnedShutdownHostAccess;
    friend class OwnedShutdownTestAccess;
    friend class SyntheticOwnedSession;
    struct Worker {
        std::thread thread;
        bool finished = false, starting = true;
        std::thread::id starter;
    };
    // Serialize joins even while the owner mutex is released for waits/join.
    // Declare before each operation's unique_lock so that lock is destroyed
    // before this guard reacquires it to release the control slot.
    enum class JoinPurpose { Drain, RetainedMaintenance, CompletedHelper };
    class JoinControl {
    public:
        JoinControl(OwnedShutdown& owner, JoinPurpose purpose) : owner_(owner) {
            std::lock_guard<std::mutex> lock(owner_.mutex_);
            // An ineligible maintenance/repeated-drain call must not briefly
            // reserve the slot and make a legitimate retirement commit fail.
            const bool eligible = purpose == JoinPurpose::Drain ? !owner_.attempted_ :
                purpose == JoinPurpose::RetainedMaintenance ?
                    owner_.attempted_ && owner_.closed_ && owner_.faulted_ &&
                    !owner_.ready_ && !owner_.retired_ && !owner_.on_obligation_thread_locked() : true;
            if (eligible && !owner_.joinControlActive_) {
                entered_ = owner_.joinControlActive_ = true;
                owner_.cv_.notify_all();
            }
        }
        JoinControl(const JoinControl&) = delete;
        JoinControl& operator=(const JoinControl&) = delete;
        ~JoinControl() {
            if (!entered_) return;
            std::lock_guard<std::mutex> lock(owner_.mutex_);
            owner_.joinControlActive_ = false;
            owner_.cv_.notify_all();
        }
        explicit operator bool() const noexcept { return entered_; }
    private:
        OwnedShutdown& owner_;
        bool entered_ = false;
    };
public:
    enum class ReapResult { ReapedRetained, PendingRetained, Refused };
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
        JoinControl control(*this, JoinPurpose::Drain);
        if (!control) return false;
        std::unique_lock<std::mutex> lock(mutex_);
        if (attempted_) return false;
        attempted_ = closed_ = true;
        stop_.store(true);
        cv_.notify_all();
        if (on_obligation_thread_locked()) {
            faulted_ = true;
            cv_.notify_all();
            return false;
        }
        if (!join_workers_until_locked(lock, deadline)) { faulted_ = true; return false; }
        ready_ = !faulted_ && std::chrono::steady_clock::now() <= deadline;
        return ready_;
    }
    // Explicit post-failure handle maintenance, not another retirement attempt.
    // ReapedRetained proves actual worker/callable/TLS and helper joins only.
    // It never changes ready_, faulted_, retired_, admission or backing ownership.
    // Retain the owner and full backing on every result; no cleanup/restart is
    // authorized. Retry PendingRetained after late completion if desired.
    // As for drain, mutex scheduling/thread creation/final helper join have no
    // hard wall-clock bound. The deadline is checked after actual helper join.
    ReapResult reap_retained_until(std::chrono::steady_clock::time_point deadline) {
        JoinControl control(*this, JoinPurpose::RetainedMaintenance);
        if (!control) return ReapResult::Refused;
        std::unique_lock<std::mutex> lock(mutex_);
        if (!attempted_ || !closed_ || !faulted_ || ready_ || retired_ || on_obligation_thread_locked())
            return ReapResult::Refused;
        return join_workers_until_locked(lock, deadline)
            ? ReapResult::ReapedRetained : ReapResult::PendingRetained;
    }
private:
    bool on_obligation_thread_locked() const {
        const auto current = std::this_thread::get_id();
        if ((transitioning_ && transitionThread_ == current) || currentWorker_ == this ||
            std::find(readerThreads_.begin(), readerThreads_.end(), current) != readerThreads_.end()) return true;
        for (const auto& w : workers_) if (w->starting && w->starter == current) return true;
        return false;
    }
    bool join_workers_until_locked(std::unique_lock<std::mutex>& lock,
                                  std::chrono::steady_clock::time_point deadline) {
        const auto done = [&] {
            if (readers_ || starting_ || transitioning_) return false;
            for (const auto& w : workers_) if (!w->finished) return false;
            return true;
        };
        if (!cv_.wait_until(lock, deadline, done)) return false;
        // No thread/TLS obligation exists when no worker was ever registered.
        // A process-retained owner must not create an otherwise unnecessary
        // joiner whose finished-but-unjoined handle would itself be retained.
        // Reader/start/transition drainage above is still required, under this
        // same lock, and closed admission prevents a new worker appearing here.
        if (workers_.empty()) {
            joined_ = true;
            return std::chrono::steady_clock::now() <= deadline;
        }
        // Joining on this control thread is not bounded (thread-local destructors
        // may still run after finished). An owned joiner supplies actual join proof.
        if (!joined_ && !joiner_.joinable()) {
            // Thread construction can allocate or reenter host diagnostics.
            // JoinControl reserves this slot while the mutex is released, so
            // such calls can refuse without deadlocking or launching a helper.
            std::thread helper;
            lock.unlock();
            try { helper = std::thread([this] {
                for (auto& w : workers_) if (w->thread.joinable()) w->thread.join();
                std::lock_guard<std::mutex> joined(mutex_);
                joined_ = true;
                cv_.notify_all();
            }); } catch (...) { lock.lock(); return false; }
            lock.lock();
            joiner_ = std::move(helper);
            cv_.notify_all();
        }
        if (!cv_.wait_until(lock, deadline, [&] { return joined_; })) return false;
        if (!join_completed_helper_locked(lock)) return false;
        return std::chrono::steady_clock::now() <= deadline;
    }
    bool join_completed_helper_locked(std::unique_lock<std::mutex>& lock) {
        if (!joiner_.joinable()) return true;
        // joined_ proves the worker joins, not the joiner's own thread exit.
        // Retained owners never reach their destructor, so reap this completed
        // helper here. Keep readiness unpublished through its actual join and
        // recheck fault/deadline afterwards; no user code runs under mutex_.
        std::thread completed = std::move(joiner_);
        lock.unlock();
        try { completed.join(); }
        catch (...) {
            lock.lock();
            joiner_ = std::move(completed);
            return false;
        }
        lock.lock();
        return true;
    }
public:
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
        // The synthetic retirement commit is terminal. A fault that acquires
        // this mutex after that commit is ordered later and cannot revoke it.
        if (retired_) return;
        faulted_ = closed_ = true;
        ready_ = false;
        stop_.store(true);
        cv_.notify_all();
    }
private:
    // After all worker joins have finished, the internal joiner itself can be
    // reaped without holding the owner mutex. It executes no user callback.
    // A still-running joiner stays owned and retained on a failed timeout.
    bool reap_completed_joiner() {
        JoinControl control(*this, JoinPurpose::CompletedHelper);
        if (!control) return false;
        std::unique_lock<std::mutex> lock(mutex_);
        if (!joined_) return true;
        return join_completed_helper_locked(lock);
    }
    // The synthetic session uses this as its final linearization point. A
    // later fault is a no-op; an earlier fault makes commit fail. It does not
    // run cleanup, user code, or release any backing while locked.
    bool commit_retired(std::chrono::steady_clock::time_point deadline) {
        if (!reap_completed_joiner()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closed_ || !attempted_ || !ready_ || faulted_ || retired_ ||
            std::chrono::steady_clock::now() > deadline) return false;
        retired_ = true;
        return true;
    }
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    inline static thread_local OwnedShutdown* currentWorker_ = nullptr;
    bool closed_ = false, attempted_ = false, ready_ = false, faulted_ = false, retired_ = false;
    size_t readers_ = 0;
    std::vector<std::thread::id> readerThreads_;
    size_t starting_ = 0;
    bool transitioning_ = false;
    bool startup_ = false;
    std::thread::id transitionThread_;
    bool joined_ = false;
    bool joinControlActive_ = false;
    std::thread joiner_;
    std::vector<std::unique_ptr<Worker>> workers_;
};
} // namespace sco
