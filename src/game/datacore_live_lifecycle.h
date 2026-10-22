// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

namespace sco::game::datacore_live {

class LifecycleGate {
public:
    enum class Status {
        Acquired,
        UpgradeDenied,
        Busy,
        InvalidLease,
    };

    class ReadLease {
    public:
        ReadLease() = default;
        ~ReadLease();
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;
        ReadLease(ReadLease&&) = delete;
        ReadLease& operator=(ReadLease&&) = delete;

        explicit operator bool() const noexcept { return gate_ != nullptr; }

    private:
        friend class LifecycleGate;
        LifecycleGate* gate_ = nullptr;
        ReadLease* previous_ = nullptr;
        std::thread::id thread_{};
        bool counted_ = false;
    };

    class ExclusiveLease {
    public:
        ExclusiveLease() = default;
        ~ExclusiveLease();
        ExclusiveLease(const ExclusiveLease&) = delete;
        ExclusiveLease& operator=(const ExclusiveLease&) = delete;
        ExclusiveLease(ExclusiveLease&&) = delete;
        ExclusiveLease& operator=(ExclusiveLease&&) = delete;

        explicit operator bool() const noexcept { return gate_ != nullptr; }

    private:
        friend class LifecycleGate;
        LifecycleGate* gate_ = nullptr;
        std::thread::id thread_{};
    };

    bool EnterRead(ReadLease& lease);
    Status EnterExclusive(ExclusiveLease& lease);
    Status TryEnterExclusive(ExclusiveLease& lease);

    // Test synchronization seam: lets a fixture observe a queued writer
    // without sleeps or polling. The timeout is only a deadlock watchdog.
    bool WaitForQueuedWriterForTesting(std::chrono::milliseconds timeout);

    // A native lifecycle detour must keep this lease alive across the complete
    // original call. Upgrade denial is impossible-by-contract reentrancy:
    // returning or forwarding without exclusion could race readers or skip
    // required engine teardown, so production handling is fail-fast.
    void EnterLifecycleOrTerminate(ExclusiveLease& lease);

private:
    friend class ReadLease;
    friend class ExclusiveLease;

    bool HasReadLeaseOnCurrentThread() const noexcept;
    void ReleaseRead(ReadLease& lease) noexcept;
    void ReleaseExclusive(ExclusiveLease& lease) noexcept;

    std::mutex mutex_;
    std::condition_variable changed_;
    size_t readers_ = 0;
    size_t waitingWriters_ = 0;
    std::thread::id writer_{};
    size_t writerDepth_ = 0;
};

// Native shutdown may occur after service Stop. Keep synchronization state
// alive for process lifetime so installed hooks never refer to a dead gate.
LifecycleGate& ProcessLifecycleGate() noexcept;

} // namespace sco::game::datacore_live
