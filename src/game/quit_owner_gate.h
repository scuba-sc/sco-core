// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "sco/owned_shutdown.h"
#include <atomic>
#include <memory>
#include <new>

namespace sco::game::host::detail {
// One legacy session. Published roots are never deleted, including during
// static teardown. This owns no engine object, product code, image, or worker.
class QuitOwnerGate {
    struct Root { std::shared_ptr<OwnedShutdown> owner; };
    inline static Root terminal_{};
    std::atomic<Root*> state_{nullptr};
public:
    enum class Publication { Published, Cancelled, Competing, Failed };
    enum class Notification { Early, Requested, Failed };
    Publication Publish(const std::shared_ptr<OwnedShutdown>& owner, bool failAllocation = false) noexcept {
        if (!owner) return Publication::Failed;
        const auto failedAllocation = [&]() noexcept {
            if (state_.load(std::memory_order_acquire) == &terminal_) {
                try { owner->request_stop(); } catch (...) {}
                return Publication::Cancelled;
            }
            return Publication::Failed;
        };
        if (failAllocation) return failedAllocation();
        auto* candidate = new (std::nothrow) Root{owner};
        if (!candidate) return failedAllocation();
        Root* empty = nullptr;
        if (state_.compare_exchange_strong(empty, candidate, std::memory_order_acq_rel, std::memory_order_acquire))
            return Publication::Published; // process-retained, intentionally never deleted
        delete candidate; // never exposed; the caller retains the supplied owner
        if (empty == &terminal_) {
            // An actual detour notification won before publication. Refuse
            // startup and attempt to close the supplied owner without a drain.
            // The notifier could not close an owner it had not yet been given.
            try { owner->request_stop(); } catch (...) {}
            return Publication::Cancelled;
        }
        return Publication::Competing;
    }
    Notification Notify() noexcept {
        Root* value = state_.load(std::memory_order_acquire);
        for (;;) {
            if (value == &terminal_) return Notification::Early;
            if (!value) {
                if (state_.compare_exchange_weak(value, &terminal_, std::memory_order_acq_rel))
                    return Notification::Early;
                continue;
            }
            try { value->owner->request_stop(); return Notification::Requested; }
            catch (...) { return Notification::Failed; }
        }
    }
};
} // namespace sco::game::host::detail
