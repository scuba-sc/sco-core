// SPDX-License-Identifier: GPL-3.0-only
#include "handoff.h"

namespace sco::game::asop_host {
namespace {
// A linked thread-local stack handles A -> B -> A recursion as well as A -> A.
// Apply it to fallback callbacks too: no callback queues behind its own frame.
struct CallbackScope {
    static thread_local CallbackScope* current;
    Handoff* handoff;
    CallbackScope* previous;
    explicit CallbackScope(Handoff* h) : handoff(h), previous(current) { current = this; }
    ~CallbackScope() { current = previous; }
    static bool Contains(Handoff* h) {
        for (auto* scope = current; scope; scope = scope->previous)
            if (scope->handoff == h) return true;
        return false;
    }
};
thread_local CallbackScope* CallbackScope::current = nullptr;
} // namespace

void Handoff::Run(Callback callback, void* context, bool inlineNow,
                  std::chrono::milliseconds deadline) {
    if (inlineNow || CallbackScope::Contains(this)) {
        CallbackScope scope(this);
        callback(context);
        return;
    }

    Call call{callback, context, false, false, {}};
    std::unique_lock lock(mutex_);
    if (!stopped_ && count_ < kCapacity) {
        pending_[count_++] = &call;
        changed_.wait_for(lock, deadline, [&] { return call.taken || stopped_; });
        if (call.taken) {
            // Shutdown and the take deadline cannot invalidate a taken frame.
            changed_.wait(lock, [&] { return call.done; });
            auto error = call.error;
            lock.unlock();
            if (error) std::rethrow_exception(error);
            return;
        }
        // Removal and take race under the same lock; exactly one side owns it.
        // Shutdown may already have removed this call from the queue.
        for (std::size_t i = 0; i < count_; ++i) {
            if (pending_[i] == &call) {
                for (std::size_t j = i + 1; j < count_; ++j)
                    pending_[j - 1] = pending_[j];
                pending_[--count_] = nullptr;
                break;
            }
        }
    }
    lock.unlock();
    CallbackScope scope(this);
    callback(context); // Untaken timeout, shutdown, or full queue: caller fallback.
}

void Handoff::Drain() {
    std::array<Call*, kCapacity> calls{};
    std::size_t count = 0;
    {
        std::lock_guard lock(mutex_);
        count = count_;
        for (std::size_t i = 0; i < count; ++i) {
            calls[i] = pending_[i];
            calls[i]->taken = true;
            pending_[i] = nullptr;
        }
        count_ = 0;
        changed_.notify_all();
    }
    for (std::size_t i = 0; i < count; ++i) {
        Call* call = calls[i];
        std::exception_ptr error;
        try {
            CallbackScope scope(this);
            call->callback(call->context); // Never hold the queue lock in callbacks.
        } catch (...) {
            error = std::current_exception();
        }
        {
            std::lock_guard lock(mutex_);
            call->error = error;
            call->done = true;
            changed_.notify_all();
            // Do not touch call after unlocking: Run may destroy its stack now.
        }
    }
}

void Handoff::Shutdown() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    for (std::size_t i = 0; i < count_; ++i) pending_[i] = nullptr;
    count_ = 0;
    changed_.notify_all();
}

std::size_t Handoff::Pending() const {
    std::lock_guard lock(mutex_);
    return count_;
}

} // namespace sco::game::asop_host
