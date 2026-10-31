// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include <atomic>
#include <cstddef>
#include <limits>
#include <mutex>
#include <thread>

// Internal ingress only. Supported compositions link these inline objects once
// into the trusted executable; separate embedded runtimes are unsupported.
// Entries cover the whole subsystem operation, including any callbacks. They
// never hold the authority lock while executing a subsystem or callout.
namespace sco::detail {
class AuthorityMutex {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    }
    void unlock() noexcept { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
#if defined(SCO_RESERVED_GATE_DEFINE_FIXTURE)
constinit AuthorityMutex g_authorityMutex;
constinit size_t g_legacyEntries = 0;
constinit bool g_legacyDirty = false;
constinit bool g_reserved = false;
#else
#if defined(SCO_RESERVED_GATE_IMPORT_FIXTURE) && SCO_RESERVED_GATE_IMPORT_FIXTURE == 1
extern AuthorityMutex g_authorityMutex;
#else
inline constinit AuthorityMutex g_authorityMutex;
#endif
#if defined(SCO_RESERVED_GATE_IMPORT_FIXTURE) && SCO_RESERVED_GATE_IMPORT_FIXTURE == 2
extern size_t g_legacyEntries;
#else
inline constinit size_t g_legacyEntries = 0;
#endif
#if defined(SCO_RESERVED_GATE_IMPORT_FIXTURE) && SCO_RESERVED_GATE_IMPORT_FIXTURE == 3
extern bool g_legacyDirty;
#else
inline constinit bool g_legacyDirty = false;
#endif
#if defined(SCO_RESERVED_GATE_IMPORT_FIXTURE) && SCO_RESERVED_GATE_IMPORT_FIXTURE == 4
extern bool g_reserved;
#else
inline constinit bool g_reserved = false;
#endif
#endif

class LegacyEntry {
public:
    explicit LegacyEntry(bool exposesOrMutates = true) noexcept {
        std::lock_guard lock(g_authorityMutex);
        if (g_reserved || g_legacyEntries == (std::numeric_limits<size_t>::max)()) return;
        ++g_legacyEntries;
        if (exposesOrMutates) g_legacyDirty = true;
        admitted_ = true;
    }
    ~LegacyEntry() {
        if (!admitted_) return;
        std::lock_guard lock(g_authorityMutex);
        --g_legacyEntries;
    }
    LegacyEntry(const LegacyEntry&) = delete;
    LegacyEntry& operator=(const LegacyEntry&) = delete;
    explicit operator bool() const noexcept { return admitted_; }
private:
    bool admitted_ = false;
};
}
