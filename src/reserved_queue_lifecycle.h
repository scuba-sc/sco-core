// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

// A sealed internal queue, not a general callback facility. Embed it in the
// retained authority capsule and compile its code into the process-lifetime
// executable. Never destroy the capsule while any caller or Claim exists,
// including after failure. All producers/workers must use this queue; it
// cannot account for external work. Close it before authority retirement,
// keep workers available to execute admitted work, and require both drains.
// The enclosing authority must hold its own commit lock across Seal and its
// terminal commit, and hold that same lock when reporting queue/authority
// faults. Seal performs no callback or destructor. This standalone class is
// not itself a cross-object atomic transaction.
// Claims require exclusive external access and do not extend capsule lifetime.
// Tasks execute in ticket order, one claim at a time. Deadline checks cannot
// bound mutex acquisition or scheduling delays.
namespace sco::reserved::detail {
class OwnedTaskQueue final {
public:
    static constexpr std::size_t capacity = 64;
    using Clock = std::chrono::steady_clock;
    enum class Operation : std::uint8_t { Set, Add };
    enum class State : std::uint8_t { Open, Closing, DrainedRetained, QuiescedRetained, FailedRetained };
    enum class EnqueueResult : std::uint8_t { Accepted, Full, Closed, Invalid, Faulted };
    enum class DrainResult : std::uint8_t { DrainedRetained, FailedRetained, Refused };

    class Claim final {
    public:
        Claim() = default;
        Claim(const Claim&) = delete;
        Claim& operator=(const Claim&) = delete;
        Claim(Claim&&) = delete;
        Claim& operator=(Claim&&) = delete;
        ~Claim() { if (owner_) owner_->Abandon(*this); }
        explicit operator bool() const noexcept { return owner_ != nullptr; }
        // Sealed operations: no foreign code, payload, or destructor.
        bool Execute() { return owner_ && owner_->Execute(*this); }
    private:
        friend class OwnedTaskQueue;
        OwnedTaskQueue* owner_ = nullptr;
        std::size_t slot_ = 0;
    };
    OwnedTaskQueue() = default;
    OwnedTaskQueue(const OwnedTaskQueue&) = delete;
    OwnedTaskQueue& operator=(const OwnedTaskQueue&) = delete;

    EnqueueResult Enqueue(Operation op, std::uint64_t value, std::uint64_t* ticket = nullptr) {
        if (op != Operation::Set && op != Operation::Add) return EnqueueResult::Invalid;
        std::lock_guard lock(mutex_);
        if (state_ == State::FailedRetained) return EnqueueResult::Faulted;
        if (state_ != State::Open) return EnqueueResult::Closed;
        if (outstanding_ == capacity || lastIssued_ == (std::numeric_limits<std::uint64_t>::max)()) return EnqueueResult::Full;
        for (std::size_t i = 0; i < capacity; ++i) {
            if (slots_[i].state == SlotState::Free) {
                const auto sequence = ++lastIssued_;
                slots_[i] = {op, value, sequence, SlotState::Queued};
                ++outstanding_;
                if (ticket) *ticket = sequence;
                cv_.notify_one();
                return EnqueueResult::Accepted;
            }
        }
        FailLocked(); // invariant failure
        return EnqueueResult::Faulted;
    }
    // Admitted work may be claimed after close. A claim accounts for it until
    // Execute completes; abandoning one faults and retains its slot.
    bool TryClaim(Claim& claim) {
        std::lock_guard lock(mutex_);
        if (claim.owner_ || (state_ != State::Open && state_ != State::Closing)) return false;
        return ClaimFirstLocked(claim);
    }
    // Wait only while the queue is open and empty. Close/fault wake workers;
    // queued work remains executable after close, before provisional drain.
    bool WaitClaim(Claim& claim) {
        std::unique_lock lock(mutex_);
        if (claim.owner_) return false;
        cv_.wait(lock, [&] {
            if (state_ != State::Open) return true;
            if (!claimed_) for (const auto& slot : slots_) if (slot.state == SlotState::Queued) return true;
            return false;
        });
        if (state_ != State::Open && state_ != State::Closing) return false;
        return ClaimFirstLocked(claim);
    }
    bool Close() {
        std::lock_guard lock(mutex_);
        if (state_ == State::Closing) return true;
        if (state_ != State::Open) return false;
        state_ = State::Closing;
        cv_.notify_all();
        return true;
    }
    void Fault() { std::lock_guard lock(mutex_); FailLocked(); }
    DrainResult DrainUntil(Clock::time_point deadline) {
        std::unique_lock lock(mutex_);
        if (state_ == State::FailedRetained) return DrainResult::FailedRetained;
        if (state_ != State::Closing || drainAttempted_) return DrainResult::Refused;
        drainAttempted_ = true;
        try {
            while (outstanding_ && state_ == State::Closing) {
                if (cv_.wait_until(lock, deadline) == std::cv_status::timeout && outstanding_) {
                    FailLocked();
                    return DrainResult::FailedRetained;
                }
            }
            if (state_ != State::Closing) return DrainResult::FailedRetained;
            if (Clock::now() > deadline) {
                FailLocked();
                return DrainResult::FailedRetained;
            }
            // Draining is provisional: faults remain sticky until Seal.
            state_ = State::DrainedRetained;
            return DrainResult::DrainedRetained;
        } catch (...) {
            FailLocked();
            return DrainResult::FailedRetained;
        }
    }
    // The outer authority serializes this with its own commit and faults.
    // Only the state changes: there is no execution, callback, or cleanup.
    bool Seal() {
        std::lock_guard lock(mutex_);
        if (state_ != State::DrainedRetained) return false;
        state_ = State::QuiescedRetained;
        return true;
    }
    State GetState() const { std::lock_guard lock(mutex_); return state_; }
    std::size_t Outstanding() const { std::lock_guard lock(mutex_); return outstanding_; }
    std::uint64_t Value() const { std::lock_guard lock(mutex_); return value_; }
    void Progress(std::uint64_t& value, std::uint64_t& completed) const {
        std::lock_guard lock(mutex_); value = value_; completed = completed_;
    }
private:
    enum class SlotState : std::uint8_t { Free, Queued, Claimed };
    struct Slot { Operation op = Operation::Set; std::uint64_t value = 0, sequence = 0; SlotState state = SlotState::Free; };
    bool ClaimFirstLocked(Claim& claim) {
        if (claimed_) return false;
        std::size_t first = capacity;
        for (std::size_t i = 0; i < capacity; ++i) {
            if (slots_[i].state == SlotState::Queued &&
                (first == capacity || slots_[i].sequence < slots_[first].sequence)) first = i;
        }
        if (first == capacity) return false;
        slots_[first].state = SlotState::Claimed;
        claimed_ = true;
        claim.owner_ = this; claim.slot_ = first;
        return true;
    }
    void FailLocked() {
        if (state_ != State::QuiescedRetained) state_ = State::FailedRetained;
        cv_.notify_all();
    }
    void Abandon(Claim& claim) {
        std::lock_guard lock(mutex_);
        claim.owner_ = nullptr;
        // Preserve the slot and count as evidence of incomplete work.
        FailLocked();
    }
    bool Execute(Claim& claim) {
        std::lock_guard lock(mutex_);
        Slot& slot = slots_[claim.slot_];
        const bool run = state_ == State::Open || state_ == State::Closing;
        if (run) {
            if (slot.op == Operation::Set) value_ = slot.value;
            else value_ += slot.value; // unsigned wrap is defined
            completed_ = slot.sequence;
        }
        slot.state = SlotState::Free;
        claimed_ = false;
        --outstanding_;
        claim.owner_ = nullptr;
        cv_.notify_all();
        return run;
    }
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::array<Slot, capacity> slots_{};
    std::size_t outstanding_ = 0;
    std::uint64_t value_ = 0;
    std::uint64_t lastIssued_ = 0, completed_ = 0;
    bool claimed_ = false;
    State state_ = State::Open;
    bool drainAttempted_ = false;
};
} // namespace sco::reserved::detail
