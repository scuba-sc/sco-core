// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "datacore_live_lifecycle.h"

#include <exception>

namespace sco::game::datacore_live {
namespace {
thread_local LifecycleGate::ReadLease* g_readLeases = nullptr;
}

LifecycleGate::ReadLease::~ReadLease() {
    if (gate_) gate_->ReleaseRead(*this);
}

LifecycleGate::ExclusiveLease::~ExclusiveLease() {
    if (gate_) gate_->ReleaseExclusive(*this);
}

bool LifecycleGate::HasReadLeaseOnCurrentThread() const noexcept {
    for (ReadLease* lease = g_readLeases; lease; lease = lease->previous_)
        if (lease->gate_ == this) return true;
    return false;
}

bool LifecycleGate::EnterRead(ReadLease& lease) {
    if (lease.gate_) return false;

    const std::thread::id current = std::this_thread::get_id();
    std::unique_lock<std::mutex> lock(mutex_);
    // Do not permit SDK reads inside native lifecycle calls: those calls can
    // expose transient owner state even though the thread owns the writer lock.
    if (writer_ == current) return false;

    // Existing readers may recurse despite a queued writer. Blocking them
    // behind that writer would deadlock: the writer waits for their outer lease.
    const bool recursiveRead = HasReadLeaseOnCurrentThread();
    changed_.wait(lock, [&] {
        return writer_ == std::thread::id{} && (recursiveRead || waitingWriters_ == 0);
    });

    ++readers_;
    lease.gate_ = this;
    lease.thread_ = current;
    lease.counted_ = true;
    lease.previous_ = g_readLeases;
    g_readLeases = &lease;
    return true;
}

LifecycleGate::Status LifecycleGate::EnterExclusive(ExclusiveLease& lease) {
    if (lease.gate_) return Status::InvalidLease;

    const std::thread::id current = std::this_thread::get_id();
    std::unique_lock<std::mutex> lock(mutex_);
    if (writer_ == current) {
        ++writerDepth_;
        lease.gate_ = this;
        lease.thread_ = current;
        return Status::Acquired;
    }
    if (HasReadLeaseOnCurrentThread()) return Status::UpgradeDenied;

    ++waitingWriters_;
    changed_.notify_all();
    changed_.wait(lock, [&] { return writer_ == std::thread::id{} && readers_ == 0; });
    --waitingWriters_;
    writer_ = current;
    writerDepth_ = 1;
    lease.gate_ = this;
    lease.thread_ = current;
    return Status::Acquired;
}

LifecycleGate::Status LifecycleGate::TryEnterExclusive(ExclusiveLease& lease) {
    if (lease.gate_) return Status::InvalidLease;

    const std::thread::id current = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(mutex_);
    if (writer_ == current) {
        ++writerDepth_;
        lease.gate_ = this;
        lease.thread_ = current;
        return Status::Acquired;
    }
    if (HasReadLeaseOnCurrentThread()) return Status::UpgradeDenied;
    if (writer_ != std::thread::id{} || readers_ != 0 || waitingWriters_ != 0)
        return Status::Busy;

    writer_ = current;
    writerDepth_ = 1;
    lease.gate_ = this;
    lease.thread_ = current;
    return Status::Acquired;
}

bool LifecycleGate::WaitForQueuedWriterForTesting(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return waitingWriters_ != 0; });
}

void LifecycleGate::EnterLifecycleOrTerminate(ExclusiveLease& lease) {
    if (EnterExclusive(lease) != Status::Acquired) std::terminate();
}

void LifecycleGate::ReleaseRead(ReadLease& lease) noexcept {
    if (lease.thread_ != std::this_thread::get_id()) std::terminate();

    ReadLease** link = &g_readLeases;
    while (*link && *link != &lease) link = &(*link)->previous_;
    if (!*link) std::terminate();
    *link = lease.previous_;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lease.counted_) {
            if (!readers_) std::terminate();
            --readers_;
        }
    }
    lease.gate_ = nullptr;
    lease.previous_ = nullptr;
    lease.thread_ = {};
    lease.counted_ = false;
    changed_.notify_all();
}

void LifecycleGate::ReleaseExclusive(ExclusiveLease& lease) noexcept {
    if (lease.thread_ != std::this_thread::get_id()) std::terminate();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (writer_ != lease.thread_ || !writerDepth_) std::terminate();
        if (--writerDepth_ == 0) writer_ = {};
    }
    lease.gate_ = nullptr;
    lease.thread_ = {};
    changed_.notify_all();
}

LifecycleGate& ProcessLifecycleGate() noexcept {
    static LifecycleGate* const gate = new LifecycleGate;
    return *gate;
}

} // namespace sco::game::datacore_live
