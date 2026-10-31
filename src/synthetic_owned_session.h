// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sco/owned_shutdown.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sco {

// Isolated synthetic patterns and byte image, resolved by a private instance of
// SignatureRegistry. No app services, global registry publication, native
// addresses, host callbacks or hooks are used. Each session retains its capsule
// for process lifetime, including after failure and wrapper destruction.
class SyntheticOwnedSession {
    struct Backing;
    struct Capsule;
public:
    static constexpr size_t MaxKeyBytes = 128;
    struct Entry { std::string key; uint64_t value; };
    // One classified synthetic callable slot; the retained context is valid
    // for process lifetime, provided the embedding module containing the thunk
    // also stays mapped. The caller supplies a readable key buffer containing
    // a NUL within MaxKeyBytes + 1 bytes and a writable output. No borrowed view or external callback escapes. This is
    // not an app or plugin ABI.
    struct Table {
        void* context;
        bool (*lookup)(void* context, const char* key, uint64_t* value) noexcept;
    };
    enum class Status { Active, Quiescing, QuiescedRetained, FailedRetained };
    enum class RetireResult { QuiescedRetained, FailedRetained, Unavailable };

    // Move-only and thread-bound. A successful lookup copies a synthetic value;
    // no pointer, reference or snapshot into the capsule escapes.
    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&&) noexcept = default;
        explicit operator bool() const noexcept;
        bool lookup(std::string_view key, uint64_t& result) const noexcept;
    private:
        friend class SyntheticOwnedSession;
        Lease(Capsule* capsule, OwnedShutdown::Read&& read) noexcept;
        Capsule* capsule_ = nullptr;
        OwnedShutdown::Read read_;
        std::thread::id thread_;
    };

    // Copies/moves the entire synthetic dataset before publication. Duplicate,
    // empty or embedded-NUL names throw std::invalid_argument before retention.
    explicit SyntheticOwnedSession(std::vector<Entry> entries);
    SyntheticOwnedSession(const SyntheticOwnedSession&) = delete;
    SyntheticOwnedSession& operator=(const SyntheticOwnedSession&) = delete;
    ~SyntheticOwnedSession() = default;

    Lease try_acquire();
    Table table() const noexcept;
    // Timed waits and the commit check use this deadline. OS scheduling, mutex
    // acquisition and reaping the completed internal joiner have no hard
    // wall-clock bound. On failure the capsule remains retained and terminal.
    // Commit and fault are ordered under the owner lock: a fault before commit
    // fails retirement; a fault after commit cannot revoke QuiescedRetained.
    RetireResult retire_until(std::chrono::steady_clock::time_point deadline);
    Status status() const noexcept;
    // There is no restart operation: this instance is terminal after retirement
    // begins. New instances own independent synthetic copies, not shared state.

#ifdef SCO_SYNTHETIC_SESSION_TESTING
    // Synthetic test seams; not present in the production interface. A worker
    // supplied here is enrolled in the same owner and must be joined by retire.
    OwnedShutdown& owner_for_test() noexcept;
    std::weak_ptr<const void> backing_for_test() const noexcept;
    void set_table_hook_for_test(void (*hook)(void*), void* context) noexcept;
    void set_table_fault_hook_for_test(void (*hook)(void*), void* context) noexcept;
    RetireResult retire_with_hook_for_test(std::chrono::steady_clock::time_point deadline,
                                          void (*before_commit)(void*), void* context);
#endif
private:
    static bool table_lookup(void* context, const char* key, uint64_t* value) noexcept;
    static std::atomic<Capsule*>& retained_capsules() noexcept;
    RetireResult retire_impl(std::chrono::steady_clock::time_point deadline
#ifdef SCO_SYNTHETIC_SESSION_TESTING
                            , void (*before_commit)(void*), void* context
#endif
                            );
    Capsule* capsule_;
};
} // namespace sco
