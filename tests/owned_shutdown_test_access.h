// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "sco/owned_shutdown.h"

namespace sco {
// Observation only. No joins, detaches, readiness changes or backing release.
class OwnedShutdownTestAccess {
public:
    static bool handles_reaped(const OwnedShutdown& owner) {
        std::lock_guard<std::mutex> lock(owner.mutex_);
        // joined_ orders worker handle reads after their actual joins. The
        // control flag also covers a helper temporarily moved out for join.
        if (!owner.joined_ || owner.joinControlActive_ || owner.joiner_.joinable()) return false;
        for (const auto& worker : owner.workers_) if (worker->thread.joinable()) return false;
        return true;
    }
    static bool helper_owned(const OwnedShutdown& owner) {
        std::lock_guard<std::mutex> lock(owner.mutex_);
        return owner.joiner_.joinable();
    }
    static bool wait_for_join_control(OwnedShutdown& owner,
                                     std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(owner.mutex_);
        return owner.cv_.wait_until(lock, deadline, [&] { return owner.joinControlActive_; });
    }
    static bool wait_for_helper(OwnedShutdown& owner,
                                std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(owner.mutex_);
        return owner.cv_.wait_until(lock, deadline, [&] { return owner.joiner_.joinable(); });
    }
};
} // namespace sco
