// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "sco/reserved_session.h"
#include "reserved_legacy_gate.h"
#include "reserved_queue_lifecycle.h"
#include "reserved_worker_lifecycle.h"
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <link.h>
#endif

namespace sco::reserved {
// Every callable slot of this sealed table is classified above: each stub
// acquires a full-call pin. Adding a slot requires extending that census.
static_assert(std::is_standard_layout_v<MockTable>);
static_assert(offsetof(MockTable, queued_progress) + sizeof(MockTable::queued_progress) == sizeof(MockTable));
constexpr bool MockTableSlotsCensused() {
    auto [size, version, context, set, get, tick, snapshot, putEntity, getEntity,
          removeEntity, entityCount, startWorker, enqueueSet, enqueueAdd, queuedValue,
          zoneCount, zoneAt, entityAt, enqueueSetTracked, enqueueAddTracked, queuedProgress] = MockTable{};
    return !size && !version && !context && !set && !get && !tick && !snapshot &&
           !putEntity && !getEntity && !removeEntity && !entityCount && !startWorker &&
           !enqueueSet && !enqueueAdd && !queuedValue && !zoneCount && !zoneAt && !entityAt &&
           !enqueueSetTracked && !enqueueAddTracked && !queuedProgress;
}
static_assert(MockTableSlotsCensused());
namespace {
// Writable and internal: distinct statically linked core instances do not
// share this identity even under identical-code folding.
unsigned char g_coreIdentity;
unsigned char g_bindingInProgress;
std::atomic<void*> g_boundAuthority{nullptr};
thread_local AuthorityImpl* g_pinAuthority = nullptr;
thread_local size_t g_pinDepth = 0;
#ifdef SCO_RESERVED_DRAIN_TESTING
// Test-only observation, with no callback or blocking work under the lock.
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> g_readerDrainObserved{false};
#endif
#ifdef SCO_RESERVED_SESSION_TESTING
std::atomic<void (*)(void*)> g_beforeCopyHook{nullptr};
std::atomic<void*> g_beforeCopyContext{nullptr};
std::atomic<void (*)(void*)> g_beforeQueuedHook{nullptr};
std::atomic<void*> g_beforeQueuedContext{nullptr};
std::atomic<void (*)(void*)> g_beforeWorkerStartHook{nullptr}, g_beforeCommitHook{nullptr}, g_tlsExitHook{nullptr};
std::atomic<void*> g_beforeWorkerStartContext{nullptr}, g_beforeCommitContext{nullptr}, g_tlsExitContext{nullptr};
std::atomic<bool> g_failNextReservation{false};
std::atomic<bool> g_failNextWorkerAllocation{false};
std::atomic<bool> g_failQueueBeforeSeal{false};
#endif

void BeforeCopy() {
#ifdef SCO_RESERVED_SESSION_TESTING
    if (auto hook = g_beforeCopyHook.load()) hook(g_beforeCopyContext.load());
#endif
}
void BeforeQueuedWork() {
#ifdef SCO_RESERVED_SESSION_TESTING
    if (auto hook = g_beforeQueuedHook.load()) hook(g_beforeQueuedContext.load());
#endif
}
void BeforeWorkerStart() {
#ifdef SCO_RESERVED_SESSION_TESTING
    if (auto hook = g_beforeWorkerStartHook.load()) hook(g_beforeWorkerStartContext.load());
#endif
}
void BeforeTerminalCommit() {
#ifdef SCO_RESERVED_SESSION_TESTING
    if (auto hook = g_beforeCommitHook.load()) hook(g_beforeCommitContext.load());
#endif
}
#ifdef SCO_RESERVED_SESSION_TESTING
struct WorkerTlsExit {
    void (*hook)(void*) = nullptr;
    void* context = nullptr;
    ~WorkerTlsExit() { if (hook) { try { hook(context); } catch (...) {} } }
};
thread_local WorkerTlsExit g_workerTlsExit;
#endif

bool ExecutableOwnsAddress(const void* address, bool executable = true) noexcept {
    if (!address) return false;
#if defined(_WIN32)
    (void)executable;
    HMODULE owner = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &owner) &&
           owner == GetModuleHandleW(nullptr);
#elif defined(__linux__)
    struct Search { uintptr_t address; bool executable; bool found; } search{reinterpret_cast<uintptr_t>(address), executable, false};
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* opaque) {
        if (info->dlpi_name && *info->dlpi_name) return 0;
        auto& s = *static_cast<Search*>(opaque);
        for (size_t i = 0; i < info->dlpi_phnum; ++i) {
            const auto& ph = info->dlpi_phdr[i];
            if (ph.p_type != PT_LOAD || !(ph.p_flags & (s.executable ? PF_X : PF_W))) continue;
            const uintptr_t start = static_cast<uintptr_t>(info->dlpi_addr) + ph.p_vaddr;
            if (s.address >= start && s.address - start < ph.p_memsz) { s.found = true; break; }
        }
        return 1;
    }, &search);
    return search.found;
#else
    (void)executable;
    return false;
#endif
}

bool ExecutableOwnsCode() noexcept {
    return ExecutableOwnsAddress(reinterpret_cast<const void*>(&CreateExecutableAuthority)) &&
           ExecutableOwnsAddress(&sco::detail::g_authorityMutex, false) &&
           ExecutableOwnsAddress(&sco::detail::g_reserved, false) &&
           ExecutableOwnsAddress(&sco::detail::g_legacyDirty, false) &&
           ExecutableOwnsAddress(&sco::detail::g_legacyEntries, false);
}

bool Valid(const Bootstrap* b) noexcept {
    return b && b->size >= sizeof(Bootstrap) && b->version == kBootstrapVersion && b->context &&
           b->bind && b->reserve && b->begin && b->publish_mock && b->finish && b->table &&
           b->enter && b->leave && b->close && b->retire_until && b->phase && b->fault;
}
} // namespace
using sco::detail::AuthorityMutex;
using sco::detail::g_authorityMutex;
using sco::detail::g_legacyEntries;
using sco::detail::g_legacyDirty;
using sco::detail::g_reserved;

class AuthorityImpl {
    struct WorkState { detail::OwnedTaskQueue queue; };
    struct Capsule {
        explicit Capsule(AuthorityImpl* a) : authority(a), table{sizeof(MockTable), kMockVersion, this,
            Set, Get, Tick, Snapshot, PutEntity, GetEntity, RemoveEntity, EntityCount,
            StartWorker, EnqueueSet, EnqueueAdd, QueuedValue, ZoneCount, ZoneAt, EntityAt,
            EnqueueSetTracked, EnqueueAddTracked, QueuedProgress} {}
        AuthorityImpl* authority;
        MockTable table;
        int64_t value = 0;
        uint64_t ticks = 0;
        MockGraph graph{};
        std::thread::id startupThread{};
        void* startupControl = nullptr;
        unsigned char secret = 0;
        bool published = false;
        size_t readers = 0;
        WorkState* work = nullptr; // owned by the rooted worker group once installed
        std::unique_ptr<ReservedWorkerLifecycle> workers;
        bool workerInitializing = false, workerStarted = false;
    };
public:
    AuthorityImpl() : bootstrap_{sizeof(Bootstrap), kBootstrapVersion, this, Bind, ReserveGraph, Begin,
        Publish, Finish, Table, Enter, Leave, CloseImpl, Retire, GetPhase, FaultImpl} {}
    const Bootstrap* bootstrap() const noexcept { return &bootstrap_; }
private:
    static AuthorityImpl* Self(void* p) noexcept { return static_cast<AuthorityImpl*>(p); }
    bool SessionIs(void* p) const noexcept { return p && p == capsule_.get(); }
    // Caller holds mutex_; fault and terminal commit are serialized here.
    static void FailLocked(AuthorityImpl& a) noexcept {
        auto phase = a.phase_.load();
        while (phase != Phase::QuiescedRetained && phase != Phase::FailedRetained &&
               !a.phase_.compare_exchange_weak(phase, Phase::FailedRetained)) {}
        if (phase != Phase::QuiescedRetained && a.capsule_ && a.capsule_->work) {
            try { a.capsule_->work->queue.Fault(); } catch (...) {}
        }
        a.cv_.notify_all();
    }
    static void ForceFault(AuthorityImpl& a) noexcept {
        std::lock_guard<AuthorityMutex> lock(a.mutex_);
        FailLocked(a);
    }
    static Result Bind(void* p, const void* identity, void** out) noexcept {
        if (out) *out = nullptr;
        if (!p || !identity || !out) return Result::BadArg;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (a.binding_.identity && a.binding_.identity != identity) return Result::Unavailable;
            if (!a.binding_.identity) a.binding_.identity = identity;
            *out = &a.binding_;
            return Result::Ok;
        } catch (...) { return Result::Failed; }
    }
    static bool ValidPosition(const double (&v)[3]) noexcept {
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    }
    static bool ValidName(const char (&name)[kMockNameBytes]) noexcept {
        return name[0] && std::memchr(name, '\0', kMockNameBytes);
    }
    static bool HasZone(const MockGraph& graph, uint64_t id) noexcept {
        if (!id) return true;
        for (uint32_t i = 0; i < graph.zoneCount; ++i) if (graph.zones[i].id == id) return true;
        return false;
    }
    static bool ValidEntity(const MockGraph& graph, const MockEntity& entity) noexcept {
        return entity.id && HasZone(graph, entity.zone) && ValidName(entity.name) && ValidPosition(entity.position);
    }
    static bool ValidGraph(const MockGraph& graph) noexcept {
        if (graph.size != sizeof(MockGraph) || graph.version != kMockGraphVersion ||
            graph.zoneCount > kMockMaxZones || graph.entityCount > kMockMaxEntities) return false;
        for (uint32_t i = 0; i < graph.zoneCount; ++i) {
            const auto& zone = graph.zones[i];
            if (!zone.id || !ValidName(zone.name) || !ValidPosition(zone.origin) || !HasZone(graph, zone.parent)) return false;
            for (uint32_t j = 0; j < i; ++j) if (graph.zones[j].id == zone.id) return false;
            uint64_t parent = zone.parent;
            for (size_t depth = 0; parent; ++depth) {
                if (depth >= graph.zoneCount || parent == zone.id) return false;
                bool found = false;
                for (uint32_t j = 0; j < graph.zoneCount; ++j) {
                    if (graph.zones[j].id == parent) { parent = graph.zones[j].parent; found = true; break; }
                }
                if (!found) return false;
            }
        }
        for (uint32_t i = 0; i < graph.entityCount; ++i) {
            if (!ValidEntity(graph, graph.entities[i])) return false;
            for (uint32_t j = 0; j < i; ++j) if (graph.entities[j].id == graph.entities[i].id) return false;
        }
        return true;
    }
    static Result ReserveGraph(void* p, void* binding, const MockGraph* graph, void** out, void** secret) noexcept {
        if (!graph) { if (out) *out = nullptr; if (secret) *secret = nullptr; return Result::BadArg; }
        return ReserveImpl(p, binding, graph, out, secret);
    }
    static Result ReserveImpl(void* p, void* binding, const MockGraph* graph, void** out, void** secret) noexcept {
        if (out) *out = nullptr;
        if (secret) *secret = nullptr;
        if (!p || !binding || !out || !secret) return Result::BadArg;
        auto* a = Self(p);
        // Candidate allocation and destruction are outside the authority lock.
#ifdef SCO_RESERVED_SESSION_TESTING
        if (g_failNextReservation.exchange(false)) return Result::TooMany;
#endif
        std::unique_ptr<Capsule> candidate;
        try { candidate.reset(new (std::nothrow) Capsule(a)); }
        catch (...) { return Result::TooMany; }
        if (!candidate) return Result::TooMany;
        candidate->graph = *graph; // complete caller-supplied by-value copy before reservation
        if (!ValidGraph(candidate->graph)) return Result::BadArg;
        Result result = Result::Unavailable;
        try {
            std::lock_guard<AuthorityMutex> lock(a->mutex_);
            if (binding == &a->binding_ && a->binding_.identity && !a->capsule_ && a->phase_ == Phase::Fresh &&
                !g_reserved && !g_legacyDirty && !g_legacyEntries) {
                a->capsule_ = std::move(candidate); // owning root before exposure
                g_reserved = true;
                a->phase_ = Phase::Reserved;
                *out = a->capsule_.get();
                *secret = &a->capsule_->secret;
                result = Result::Ok;
            }
        } catch (...) { result = Result::Failed; }
        return result;
    }
    bool SecretIs(void* p) const noexcept { return capsule_ && p == &capsule_->secret; }
    static Result Begin(void* p, void* session, void* secret, void* control) noexcept {
        if (!p || !session || !secret || !control || control == session) return Result::BadArg;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (!a.SessionIs(session) || !a.SecretIs(secret) || a.phase_ != Phase::Reserved) return Result::Unavailable;
            a.capsule_->startupThread = std::this_thread::get_id();
            a.capsule_->startupControl = control;
            auto expected = Phase::Reserved;
            if (!a.phase_.compare_exchange_strong(expected, Phase::Starting)) return Result::Unavailable;
            return Result::Ok;
        } catch (...) { ForceFault(*Self(p)); return Result::Failed; }
    }
    static bool ControlValid(const AuthorityImpl& a, void* session, void* secret, void* handle) noexcept {
        return a.SessionIs(session) && a.SecretIs(secret) && handle && a.capsule_->startupControl == handle && a.phase_ == Phase::Starting &&
               a.capsule_->startupThread == std::this_thread::get_id();
    }
    static Result Publish(void* p, void* session, void* secret, void* handle) noexcept {
        if (!p || !session || !secret || !handle) return Result::BadArg;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (!ControlValid(a, session, secret, handle) || a.capsule_->published) return Result::Unavailable;
            // Descriptor and table are preallocated in the rooted capsule.
            a.capsule_->published = true;
            return a.phase_ == Phase::Starting ? Result::Ok : Result::Unavailable;
        } catch (...) { ForceFault(*Self(p)); return Result::Failed; }
    }
    static Result Finish(void* p, void* session, void* secret, void* handle) noexcept {
        if (!p || !session || !secret || !handle) return Result::BadArg;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (!ControlValid(a, session, secret, handle) || !a.capsule_->published) return Result::Unavailable;
            auto expected = Phase::Starting;
            if (!a.phase_.compare_exchange_strong(expected, Phase::Running)) return Result::Unavailable;
            a.capsule_->startupThread = {};
            a.capsule_->startupControl = nullptr;
            return Result::Ok;
        } catch (...) { ForceFault(*Self(p)); return Result::Failed; }
    }
    static const MockTable* Table(void* p, void* session, void* secret) noexcept {
        if (!p || !session || !secret) return nullptr;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            return a.SessionIs(session) && a.SecretIs(secret) && a.phase_ == Phase::Running && a.capsule_->published
                ? &a.capsule_->table : nullptr;
        } catch (...) { ForceFault(*Self(p)); return nullptr; }
    }
    static Result Enter(void* p, void* session, CallPin* pin) noexcept {
        if (!p || !session || !pin) return Result::BadArg;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (pin->session_) return Result::BadArg;
            if (!a.SessionIs(session) || a.phase_ != Phase::Running) return Result::Unavailable;
            if ((g_pinDepth && g_pinAuthority != &a) || g_pinDepth == std::numeric_limits<size_t>::max() ||
                a.capsule_->readers == std::numeric_limits<size_t>::max()) return Result::TooMany;
            ++a.capsule_->readers;
            ++g_pinDepth;
            g_pinAuthority = &a;
            if (a.phase_ != Phase::Running) {
                --a.capsule_->readers;
                if (!--g_pinDepth) g_pinAuthority = nullptr;
                return Result::Unavailable;
            }
            pin->authority_ = &a;
            pin->session_ = session;
            pin->thread_ = std::this_thread::get_id();
            pin->leave_ = Leave;
            return Result::Ok;
        } catch (...) { ForceFault(*Self(p)); return Result::Failed; }
    }
    static void Leave(void* p, CallPin* pin) noexcept {
        if (!p || !pin) return;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (!a.SessionIs(pin->session_)) { FailLocked(a); return; }
            if (pin->thread_ != std::this_thread::get_id() || !g_pinDepth || g_pinAuthority != &a) {
                FailLocked(a);
            } else {
                if (!--g_pinDepth) g_pinAuthority = nullptr;
            }
            if (a.capsule_->readers) --a.capsule_->readers;
            else FailLocked(a);
            pin->session_ = nullptr;
            pin->leave_ = nullptr;
            a.cv_.notify_all();
        } catch (...) {
            // No stack addresses are retained by the authority. The count may
            // remain outstanding, but failure permanently prevents commit.
            ForceFault(*Self(p));
            pin->session_ = nullptr;
            pin->leave_ = nullptr;
        }
    }
    static Result CloseImpl(void* p, void* session, void* secret) noexcept {
        if (!p || !session || !secret) return Result::BadArg;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (!a.SessionIs(session) || !a.SecretIs(secret)) return Result::Unavailable;
            if (a.phase_ == Phase::Closing) return Result::Ok;
            if (a.phase_ == Phase::Starting || a.phase_ == Phase::Reserved) {
                FailLocked(a);
                a.cv_.notify_all();
                return Result::Failed;
            }
            if (a.phase_ != Phase::Running) return Result::Unavailable;
            if (a.capsule_->work && !a.capsule_->work->queue.Close()) {
                FailLocked(a);
                return Result::Failed;
            }
            auto expected = Phase::Running;
            if (!a.phase_.compare_exchange_strong(expected, Phase::Closing)) return Result::Failed;
            a.cv_.notify_all();
            return Result::Ok;
        } catch (...) { ForceFault(*Self(p)); return Result::Failed; }
    }
    static RetireResult Retire(void* p, void* session, void* secret, std::chrono::steady_clock::time_point deadline) noexcept {
        if (!p || !session || !secret) return RetireResult::Refused;
        try {
            auto& a = *Self(p);
            std::unique_lock<AuthorityMutex> lock(a.mutex_);
            if (!a.SessionIs(session) || !a.SecretIs(secret)) return RetireResult::Refused;
            if (a.phase_ == Phase::FailedRetained) return RetireResult::FailedRetained;
            if (a.phase_ != Phase::Closing || a.retirementAttempted_) return RetireResult::Refused;
            a.retirementAttempted_ = true;
            if (g_pinDepth && g_pinAuthority == &a) {
                FailLocked(a);
                a.cv_.notify_all();
                return RetireResult::FailedRetained;
            }
            while (a.capsule_->readers && a.phase_ == Phase::Closing) {
#ifdef SCO_RESERVED_DRAIN_TESTING
                // The reader count and Closing phase are observed while holding
                // the authority lock; no arbitrary test callback runs here.
                g_readerDrainObserved.store(true, std::memory_order_release);
#endif
                if (a.cv_.wait_until(lock, deadline) == std::cv_status::timeout && a.capsule_->readers) {
                    FailLocked(a);
                    a.cv_.notify_all();
                    return RetireResult::FailedRetained;
                }
            }
            if (a.phase_ != Phase::Closing) return RetireResult::FailedRetained;
            if (std::chrono::steady_clock::now() > deadline) {
                FailLocked(a);
                return RetireResult::FailedRetained;
            }
            WorkState* work = a.capsule_->work;
            ReservedWorkerLifecycle* workers = a.capsule_->workers.get();
            const bool workerStarted = a.capsule_->workerStarted;
            if (work) {
                // A start may have installed its rooted group while Close was
                // in progress; all start pins are now drained.
                if (work->queue.GetState() == detail::OwnedTaskQueue::State::Open && !work->queue.Close()) {
                    FailLocked(a);
                    return RetireResult::FailedRetained;
                }
                lock.unlock();
                const auto drained = work->queue.DrainUntil(deadline);
                const auto joined = workerStarted && workers ? workers->retire_until(deadline)
                    : ReservedWorkerLifecycle::Result::Joined;
                BeforeTerminalCommit(); // test-only, outside authority lock
                lock.lock();
#ifdef SCO_RESERVED_SESSION_TESTING
                if (g_failQueueBeforeSeal.exchange(false)) work->queue.Fault();
#endif
                if (drained != detail::OwnedTaskQueue::DrainResult::DrainedRetained ||
                    joined != ReservedWorkerLifecycle::Result::Joined || a.phase_ != Phase::Closing ||
                    std::chrono::steady_clock::now() > deadline || !work->queue.Seal()) {
                    FailLocked(a);
                    return RetireResult::FailedRetained;
                }
            }
            // No allocation, callback, destructor or unmap in this commit.
            auto expected = Phase::Closing;
            if (!a.phase_.compare_exchange_strong(expected, Phase::QuiescedRetained))
                return expected == Phase::FailedRetained ? RetireResult::FailedRetained : RetireResult::Refused;
            return RetireResult::QuiescedRetained;
        } catch (...) {
            FaultImpl(p, session, secret);
            return RetireResult::FailedRetained;
        }
    }
    static Phase GetPhase(void* p, void* session, void* secret) noexcept {
        if (!p || !session || !secret) return Phase::FailedRetained;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            return a.SessionIs(session) && a.SecretIs(secret) ? a.phase_.load() : Phase::FailedRetained;
        } catch (...) { ForceFault(*Self(p)); return Phase::FailedRetained; }
    }
    static void FaultImpl(void* p, void* session, void* secret) noexcept {
        if (!p || !session || !secret) return;
        try {
            auto& a = *Self(p);
            std::lock_guard<AuthorityMutex> lock(a.mutex_);
            if (a.SessionIs(session) && a.SecretIs(secret)) FailLocked(a);
        } catch (...) { ForceFault(*Self(p)); }
    }
    static Result Set(void* p, int64_t value) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        try {
            std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
            if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
            c->value = value;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result Get(void* p, int64_t* out) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!out) return Result::BadArg;
        try {
            int64_t value;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
                value = c->value;
            }
            BeforeCopy();
            *out = value; // pin covers the complete output copy
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result Tick(void* p, uint64_t amount) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        try {
            std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
            if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
            if (amount > std::numeric_limits<uint64_t>::max() - c->ticks) return Result::TooMany;
            c->ticks += amount;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result Snapshot(void* p, int64_t* valueOut, uint64_t* ticksOut) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!valueOut || !ticksOut) return Result::BadArg;
        try {
            int64_t value; uint64_t ticks;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
                value = c->value; ticks = c->ticks;
            }
            BeforeCopy();
            *valueOut = value; *ticksOut = ticks;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result PutEntity(void* p, const MockEntity* input) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!input) return Result::BadArg;
        const MockEntity entity = *input;
        try {
            std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
            if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
            if (!ValidEntity(c->graph, entity)) return Result::BadArg;
            for (uint32_t i = 0; i < c->graph.entityCount; ++i) {
                if (c->graph.entities[i].id == entity.id) { c->graph.entities[i] = entity; return Result::Ok; }
            }
            if (c->graph.entityCount == kMockMaxEntities) return Result::TooMany;
            c->graph.entities[c->graph.entityCount++] = entity;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result GetEntity(void* p, uint64_t id, MockEntity* out) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!out || !id) return Result::BadArg;
        try {
            MockEntity entity{};
            bool found = false;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
                for (uint32_t i = 0; i < c->graph.entityCount; ++i) {
                    if (c->graph.entities[i].id == id) { entity = c->graph.entities[i]; found = true; break; }
                }
            }
            if (!found) return Result::NotFound;
            BeforeCopy();
            *out = entity;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result RemoveEntity(void* p, uint64_t id) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!id) return Result::BadArg;
        try {
            std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
            if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
            for (uint32_t i = 0; i < c->graph.entityCount; ++i) {
                if (c->graph.entities[i].id != id) continue;
                for (uint32_t j = i + 1; j < c->graph.entityCount; ++j) c->graph.entities[j - 1] = c->graph.entities[j];
                --c->graph.entityCount;
                return Result::Ok;
            }
            return Result::NotFound;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result EntityCount(void* p, uint32_t* out) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!out) return Result::BadArg;
        try {
            uint32_t count;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
                count = c->graph.entityCount;
            }
            BeforeCopy();
            *out = count;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result StartWorker(void* p) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        try {
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ != Phase::Running || c->workerInitializing || c->workers)
                    return Result::Unavailable;
                c->workerInitializing = true;
            }
            BeforeWorkerStart(); // test-only, outside authority lock
#ifdef SCO_RESERVED_SESSION_TESTING
            if (g_failNextWorkerAllocation.exchange(false)) throw std::bad_alloc();
#endif
            // Construct the owned payload and group outside all authority locks.
            auto work = std::make_unique<WorkState>();
            auto* const workPtr = work.get();
            auto workers = std::make_unique<ReservedWorkerLifecycle>(std::move(work));
            std::unique_lock<AuthorityMutex> lock(c->authority->mutex_);
            c->work = workPtr;
            c->workers = std::move(workers); // rooted capsule owns group wrapper
            c->workerInitializing = false;
            if (c->authority->phase_ != Phase::Running) return Result::Unavailable;
            auto start = c->workers->reserve_start();
            if (!start) { FailLocked(*c->authority); return Result::Failed; }
            lock.unlock();
            const bool launched = start.launch([c, workPtr](const std::atomic<bool>&) {
                try {
#ifdef SCO_RESERVED_SESSION_TESTING
                    g_workerTlsExit.hook = g_tlsExitHook.load();
                    g_workerTlsExit.context = g_tlsExitContext.load();
#endif
                    for (;;) {
                        detail::OwnedTaskQueue::Claim claim;
                        if (!workPtr->queue.WaitClaim(claim)) break;
                        BeforeQueuedWork(); // test-only; no foreign production callback
                        if (!claim.Execute()) break;
                    }
                } catch (...) {
                    FaultImpl(c->authority, c, &c->secret);
                    throw;
                }
            });
            {
                std::lock_guard<AuthorityMutex> hold(c->authority->mutex_);
                if (!launched) { FailLocked(*c->authority); return Result::Failed; }
                c->workerStarted = true;
                if (c->authority->phase_ != Phase::Running) return Result::Unavailable;
            }
            return Result::Ok;
        } catch (...) {
            FaultImpl(c->authority, c, &c->secret);
            return Result::Failed;
        }
    }
    static Result Enqueue(void* p, detail::OwnedTaskQueue::Operation operation, uint64_t value, uint64_t* ticket, bool tracked) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (tracked && !ticket) return Result::BadArg;
        try {
            uint64_t issued = 0;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ != Phase::Running || !c->work || !c->workerStarted) return Result::Unavailable;
                switch (c->work->queue.Enqueue(operation, value, &issued)) {
                case detail::OwnedTaskQueue::EnqueueResult::Accepted: break;
                case detail::OwnedTaskQueue::EnqueueResult::Full: return Result::TooMany;
                case detail::OwnedTaskQueue::EnqueueResult::Invalid: return Result::BadArg;
                case detail::OwnedTaskQueue::EnqueueResult::Closed: return Result::Unavailable;
                case detail::OwnedTaskQueue::EnqueueResult::Faulted: FailLocked(*c->authority); return Result::Failed;
                }
            }
            if (!issued) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
            if (ticket) { BeforeCopy(); *ticket = issued; }
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result EnqueueSet(void* p, uint64_t value) noexcept { return Enqueue(p, detail::OwnedTaskQueue::Operation::Set, value, nullptr, false); }
    static Result EnqueueAdd(void* p, uint64_t value) noexcept { return Enqueue(p, detail::OwnedTaskQueue::Operation::Add, value, nullptr, false); }
    static Result EnqueueSetTracked(void* p, uint64_t value, uint64_t* ticket) noexcept { return Enqueue(p, detail::OwnedTaskQueue::Operation::Set, value, ticket, true); }
    static Result EnqueueAddTracked(void* p, uint64_t value, uint64_t* ticket) noexcept { return Enqueue(p, detail::OwnedTaskQueue::Operation::Add, value, ticket, true); }
    static Result QueuedValue(void* p, uint64_t* out) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!out) return Result::BadArg;
        try {
            uint64_t value;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained || !c->work || !c->workerStarted) return Result::Unavailable;
                value = c->work->queue.Value();
            }
            BeforeCopy();
            *out = value;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result QueuedProgress(void* p, uint64_t* valueOut, uint64_t* completedOut) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!valueOut || !completedOut || valueOut == completedOut) return Result::BadArg;
        try {
            uint64_t value, completed;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained || !c->work || !c->workerStarted) return Result::Unavailable;
                c->work->queue.Progress(value, completed);
            }
            BeforeCopy(); *valueOut = value; *completedOut = completed;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result ZoneCount(void* p, uint32_t* out) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!out) return Result::BadArg;
        try {
            uint32_t count;
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
                count = c->graph.zoneCount;
            }
            BeforeCopy(); *out = count;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result ZoneAt(void* p, uint32_t index, MockZone* out) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!out) return Result::BadArg;
        try {
            MockZone value{};
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
                if (index >= c->graph.zoneCount) return Result::NotFound;
                value = c->graph.zones[index];
            }
            BeforeCopy(); *out = value;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    static Result EntityAt(void* p, uint32_t index, MockEntity* out) noexcept {
        if (!p) return Result::BadArg;
        auto* c = static_cast<Capsule*>(p);
        CallPin pin;
        const auto admitted = Enter(c->authority, c, &pin);
        if (admitted != Result::Ok) return admitted;
        if (!out) return Result::BadArg;
        try {
            MockEntity value{};
            {
                std::lock_guard<AuthorityMutex> lock(c->authority->mutex_);
                if (c->authority->phase_ == Phase::FailedRetained) return Result::Unavailable;
                if (index >= c->graph.entityCount) return Result::NotFound;
                value = c->graph.entities[index];
            }
            BeforeCopy(); *out = value;
            return Result::Ok;
        } catch (...) { FaultImpl(c->authority, c, &c->secret); return Result::Failed; }
    }
    AuthorityMutex& mutex_ = g_authorityMutex;
    std::condition_variable_any cv_;
    struct BindingRecord { const void* identity = nullptr; } binding_;
    std::unique_ptr<Capsule> capsule_; // authority itself is process-rooted
    std::atomic<Phase> phase_{Phase::Fresh};
    bool retirementAttempted_ = false;
    Bootstrap bootstrap_;
};

const Bootstrap* CreateExecutableAuthority() noexcept {
    if (!ExecutableOwnsCode()) return nullptr;
    static std::atomic<AuthorityImpl*> root{nullptr};
    if (auto* current = root.load(std::memory_order_acquire)) return current->bootstrap();
    AuthorityImpl* candidate = nullptr;
    try { candidate = new (std::nothrow) AuthorityImpl(); } catch (...) { return nullptr; }
    if (!candidate) return nullptr;
    AuthorityImpl* expected = nullptr;
    if (!root.compare_exchange_strong(expected, candidate, std::memory_order_release, std::memory_order_acquire)) {
        delete candidate;
        return expected->bootstrap();
    }
    return candidate->bootstrap();
}
Result BindCoreInstance(const Bootstrap* b, CoreBinding& binding) noexcept {
    if (!Valid(b) || binding.handle_) return Result::BadArg;
    // Supported modules import the executable's one core implementation. A
    // statically embedded copy fails executable ownership before binding.
    const auto* trusted = CreateExecutableAuthority();
    if (!trusted || b->context != trusted->context || b->bind != trusted->bind ||
        b->reserve != trusted->reserve || b->begin != trusted->begin ||
        b->publish_mock != trusted->publish_mock || b->finish != trusted->finish ||
        b->table != trusted->table || b->enter != trusted->enter || b->leave != trusted->leave ||
        b->close != trusted->close || b->retire_until != trusted->retire_until ||
        b->phase != trusted->phase || b->fault != trusted->fault) return Result::Unavailable;
    if (!ExecutableOwnsAddress(reinterpret_cast<const void*>(b->bind)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->reserve)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->begin)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->publish_mock)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->finish)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->table)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->enter)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->leave)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->close)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->retire_until)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->phase)) ||
        !ExecutableOwnsAddress(reinterpret_cast<const void*>(b->fault))) return Result::Unavailable;
    if (b->context == &g_bindingInProgress) return Result::BadArg;
    void* expected = nullptr;
    if (!g_boundAuthority.compare_exchange_strong(expected, &g_bindingInProgress)) return Result::Unavailable;
    void* handle = nullptr;
    // Never invoke a supplied function while holding a local mutex. Reentry
    // sees the in-progress marker and refuses rather than waiting on itself.
    const auto result = b->bind(b->context, &g_coreIdentity, &handle);
    if (result == Result::Ok && handle) {
        binding.bootstrap_ = *b; binding.handle_ = handle;
        g_boundAuthority.store(b->context);
        return Result::Ok;
    }
    g_boundAuthority.store(nullptr);
    return result == Result::Ok ? Result::Failed : result;
}
CallPin::~CallPin() { if (leave_) leave_(authority_, this); }
CallPin::operator bool() const noexcept { return session_ && thread_ == std::this_thread::get_id(); }
StartupControl::~StartupControl() {
    if (active_ && bootstrap_.fault) bootstrap_.fault(bootstrap_.context, session_, secret_);
}
Session::~Session() { if (handle_ && secret_ && bootstrap_.fault) bootstrap_.fault(bootstrap_.context, handle_, secret_); }
Result ReserveSession(CoreBinding& binding, const MockGraph& graph, Session& session) noexcept {
    if (!binding.handle_ || session.handle_) return Result::BadArg;
    void* handle = nullptr; void* secret = nullptr;
    const auto result = binding.bootstrap_.reserve(binding.bootstrap_.context, binding.handle_, &graph, &handle, &secret);
    if (result == Result::Ok) { session.bootstrap_ = binding.bootstrap_; session.handle_ = handle; session.secret_ = secret; }
    return result;
}
Result BeginStartup(Session& session, StartupControl& control) noexcept {
    if (!session.handle_ || !session.secret_ || control.active_) return Result::BadArg;
    const auto& b = session.bootstrap_;
    const auto result = b.begin(b.context, session.handle_, session.secret_, &control);
    if (result == Result::Ok) { control.bootstrap_ = b; control.session_ = session.handle_; control.secret_ = session.secret_; control.handle_ = &control; control.active_ = true; }
    return result;
}
Result PublishTypedProvider(StartupControl& control) noexcept {
    return control.active_ ? control.bootstrap_.publish_mock(control.bootstrap_.context, control.session_, control.secret_, control.handle_) : Result::Unavailable;
}
Result FinishStartup(StartupControl& control) noexcept {
    if (!control.active_) return Result::Unavailable;
    const auto result = control.bootstrap_.finish(control.bootstrap_.context, control.session_, control.secret_, control.handle_);
    if (result == Result::Ok) control.active_ = false;
    return result;
}
const MockTable* QueryMock(Session& session) noexcept {
    return session.handle_ && session.secret_ ? session.bootstrap_.table(session.bootstrap_.context, session.handle_, session.secret_) : nullptr;
}
Result TryEnter(Session& session, CallPin& pin) noexcept {
    return session.handle_ && session.secret_ ? session.bootstrap_.enter(session.bootstrap_.context, session.handle_, &pin) : Result::BadArg;
}
Result Close(Session& session) noexcept {
    return session.handle_ && session.secret_ ? session.bootstrap_.close(session.bootstrap_.context, session.handle_, session.secret_) : Result::BadArg;
}
RetireResult RetireUntil(Session& session, std::chrono::steady_clock::time_point deadline) noexcept {
    return session.handle_ && session.secret_ ? session.bootstrap_.retire_until(session.bootstrap_.context, session.handle_, session.secret_, deadline) : RetireResult::Refused;
}
#ifdef SCO_RESERVED_DRAIN_TESTING
namespace testing {
bool ReaderDrainObserved() noexcept {
    return g_readerDrainObserved.load(std::memory_order_acquire);
}
}
#endif
Phase Status(Session& session) noexcept {
    return session.handle_ && session.secret_ ? session.bootstrap_.phase(session.bootstrap_.context, session.handle_, session.secret_) : Phase::FailedRetained;
}
void Fault(Session& session) noexcept { if (session.handle_ && session.secret_) session.bootstrap_.fault(session.bootstrap_.context, session.handle_, session.secret_); }
#ifdef SCO_RESERVED_SESSION_TESTING
namespace testing {
void SetBeforeCopyHook(void (*hook)(void*), void* context) noexcept {
    g_beforeCopyContext.store(context);
    g_beforeCopyHook.store(hook);
}
void SetBeforeQueuedWorkHook(void (*hook)(void*), void* context) noexcept {
    g_beforeQueuedContext.store(context);
    g_beforeQueuedHook.store(hook);
}
void SetBeforeWorkerStartHook(void (*hook)(void*), void* context) noexcept {
    g_beforeWorkerStartContext.store(context); g_beforeWorkerStartHook.store(hook);
}
void SetBeforeWorkerPublishHook(void (*hook)(void*) noexcept, void* context) noexcept {
    sco::reserved_worker_testing::SetBeforePublish(hook, context);
}
void SetBeforeTerminalCommitHook(void (*hook)(void*), void* context) noexcept {
    g_beforeCommitContext.store(context); g_beforeCommitHook.store(hook);
}
void SetWorkerTlsExitHook(void (*hook)(void*), void* context) noexcept {
    g_tlsExitContext.store(context); g_tlsExitHook.store(hook);
}
void FailNextReservationAllocation() noexcept { g_failNextReservation.store(true); }
void FailNextWorkerAllocation() noexcept { g_failNextWorkerAllocation.store(true); }
void FailNextWorkerLaunch() noexcept { sco::reserved_worker_testing::failNextLaunch.store(true); }
void FailQueueBeforeSeal() noexcept { g_failQueueBeforeSeal.store(true); }
}
#endif
} // namespace sco::reserved
