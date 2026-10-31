// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once

// Host-source only. This is separate from the frozen plugin ABI. A supported
// composition links core once into the trusted executable and keeps it mapped.
// This C++20 bootstrap requires the same compiler, STL, CRT and steady-clock ABI
// in all participating modules; it is not a portable C ABI. Modules import the
// executable's core implementation instead of embedding another runtime. This
// experimental header has only partial legacy mutation-gate coverage; it is
// not yet a supported host composition. Rebuild every module in lockstep;
// binaries built against earlier candidate wrapper layouts are unsupported.
#include "sco/runtime.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#if defined(_WIN32) && defined(SCO_RESERVED_SESSION_EXPORTS)
#define SCO_RESERVED_API __declspec(dllexport)
#elif defined(_WIN32) && defined(SCO_RESERVED_SESSION_IMPORTS)
#define SCO_RESERVED_API __declspec(dllimport)
#else
#define SCO_RESERVED_API
#endif

namespace sco::reserved {

inline constexpr uint32_t kBootstrapVersion = 3;
inline constexpr uint32_t kMockVersion = 5;
inline constexpr uint32_t kMockGraphVersion = 1;
inline constexpr size_t kMockMaxZones = 64;
inline constexpr size_t kMockMaxEntities = 1024;
inline constexpr size_t kMockNameBytes = 96;
enum class Phase : uint32_t { Fresh, Reserved, Starting, Running, Closing, QuiescedRetained, FailedRetained };
enum class RetireResult : uint32_t { QuiescedRetained, FailedRetained, Refused };
class CallPin;
class StartupControl;
class CoreBinding;
class Session;
class AuthorityImpl;
struct Bootstrap;
struct MockTable;
struct MockGraph;

// Host-private operations. They are C++ source interfaces, not plugin ABI.
SCO_RESERVED_API const Bootstrap* CreateExecutableAuthority() noexcept;
SCO_RESERVED_API Result BindCoreInstance(const Bootstrap*, CoreBinding&) noexcept;
SCO_RESERVED_API Result ReserveSession(CoreBinding&, const MockGraph&, Session&) noexcept;
SCO_RESERVED_API Result BeginStartup(Session&, StartupControl&) noexcept;
SCO_RESERVED_API Result PublishTypedProvider(StartupControl&) noexcept;
SCO_RESERVED_API Result FinishStartup(StartupControl&) noexcept;
SCO_RESERVED_API const MockTable* QueryMock(Session&) noexcept;
SCO_RESERVED_API Result TryEnter(Session&, CallPin&) noexcept;
SCO_RESERVED_API Result Close(Session&) noexcept;
SCO_RESERVED_API RetireResult RetireUntil(Session&, std::chrono::steady_clock::time_point) noexcept;
SCO_RESERVED_API Phase Status(Session&) noexcept;
SCO_RESERVED_API void Fault(Session&) noexcept;

// Fully by-value host data: no borrowed pointers, callbacks, handles or views.
// Names must be nonempty and NUL terminated; positions must be finite. IDs are
// nonzero and unique within each of the separate zone and entity namespaces.
// A zero zone parent denotes the world. The supported graph contains only
// these fields and does not stand in for the complete engine mock scene.
struct MockZone {
    uint64_t id = 0, parent = 0;
    double origin[3]{};
    char name[kMockNameBytes]{};
};
struct MockEntity {
    uint64_t id = 0, zone = 0;
    double position[3]{};
    uint32_t flags = 0;
    char name[kMockNameBytes]{};
};
struct MockGraph {
    uint32_t size = sizeof(MockGraph), version = kMockGraphVersion;
    uint32_t zoneCount = 0, entityCount = 0;
    MockZone zones[kMockMaxZones]{};
    MockEntity entities[kMockMaxEntities]{};
};

// Every slot acquires a whole-call admission, including its output copy.
// Graph reads return independent values; index order can change after removal.
// put_entity inserts or replaces by id. Zone membership must already exist.
// The queue is a separate unsigned mock value (not an engine Tick or cvar).
// It owns Set/Add payloads by value and executes them in ticket order; Add
// wraps modulo 2^64. Tracked
// enqueue returns a nonzero ticket; queued_progress returns a copied value and
// the last completed ticket. Completion >= ticket proves that task completed.
// The two progress outputs must be distinct valid locations.
// The single worker may be started once. Arbitrary functions, foreign callback
// contexts, raw views, plugins, scripts and additional workers are unavailable.
struct MockTable {
    uint32_t size;
    uint32_t version;
    void* context;
    Result (*set)(void*, int64_t) noexcept;
    Result (*get)(void*, int64_t*) noexcept;
    Result (*tick)(void*, uint64_t) noexcept;
    Result (*snapshot)(void*, int64_t*, uint64_t*) noexcept;
    Result (*put_entity)(void*, const MockEntity*) noexcept;
    Result (*get_entity)(void*, uint64_t, MockEntity*) noexcept;
    Result (*remove_entity)(void*, uint64_t) noexcept;
    Result (*entity_count)(void*, uint32_t*) noexcept;
    Result (*start_worker)(void*) noexcept;
    Result (*enqueue_set)(void*, uint64_t) noexcept;
    Result (*enqueue_add)(void*, uint64_t) noexcept;
    Result (*queued_value)(void*, uint64_t*) noexcept;
    Result (*zone_count)(void*, uint32_t*) noexcept;
    Result (*zone_at)(void*, uint32_t, MockZone*) noexcept;
    Result (*entity_at)(void*, uint32_t, MockEntity*) noexcept;
    Result (*enqueue_set_tracked)(void*, uint64_t, uint64_t*) noexcept;
    Result (*enqueue_add_tracked)(void*, uint64_t, uint64_t*) noexcept;
    Result (*queued_progress)(void*, uint64_t*, uint64_t*) noexcept;
};

// The executable creates and owns this bootstrap for the process lifetime.
// Each published version is immutable; extension requires a new version/type. Modules must
// import this table rather than embed another runtime. This does not detect
// unmanaged or hostile copies that never bind. The bootstrap and control handles
// are trusted host internals, not an access-control boundary; don't expose them
// to arbitrary callbacks or untrusted modules.
struct Bootstrap {
    uint32_t size;
    uint32_t version;
    void* context;
    Result (*bind)(void*, const void*, void**) noexcept;
    Result (*reserve)(void*, void*, const MockGraph*, void**, void**) noexcept;
    Result (*begin)(void*, void*, void*, void*) noexcept;
    Result (*publish_mock)(void*, void*, void*, void*) noexcept;
    Result (*finish)(void*, void*, void*, void*) noexcept;
    const MockTable* (*table)(void*, void*, void*) noexcept;
    Result (*enter)(void*, void*, CallPin*) noexcept;
    void (*leave)(void*, CallPin*) noexcept;
    Result (*close)(void*, void*, void*) noexcept;
    RetireResult (*retire_until)(void*, void*, void*, std::chrono::steady_clock::time_point) noexcept;
    Phase (*phase)(void*, void*, void*) noexcept;
    void (*fault)(void*, void*, void*) noexcept;
};

// Refuses when the implementation's code is not in the main executable, or
// process-lifetime backing cannot be allocated. No module-local fallback is
// installed. The returned bootstrap and its backing are never destroyed.
SCO_RESERVED_API const Bootstrap* CreateExecutableAuthority() noexcept;

// Supplies this core instance's non-foldable identity to the trusted table.
// The table is size/version checked before any function field is read.
SCO_RESERVED_API Result BindCoreInstance(const Bootstrap* bootstrap, CoreBinding&) noexcept;

class CoreBinding {
public:
    CoreBinding() = default;
    CoreBinding(const CoreBinding&) = delete;
    CoreBinding& operator=(const CoreBinding&) = delete;
    explicit operator bool() const noexcept { return handle_ != nullptr; }
private:
    friend SCO_RESERVED_API Result BindCoreInstance(const Bootstrap*, CoreBinding&) noexcept;
    friend SCO_RESERVED_API Result ReserveSession(CoreBinding&, const MockGraph&, Session&) noexcept;
    Bootstrap bootstrap_{};
    void* handle_ = nullptr;
};

// The capsule and reservation remain rooted after this wrapper's destruction.
// An unfinished/nonterminal wrapper faults its session; it never releases it.
// Its owner must keep the wrapper alive while any other thread uses it. Its bool
// conversion reports only that a reservation exists; it does not mean Running
// or imply that backing may be released. Keep it out of external callbacks.
class Session {
public:
    Session() = default;
    SCO_RESERVED_API ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    explicit operator bool() const noexcept { return handle_ && secret_; }
private:
    friend SCO_RESERVED_API Result ReserveSession(CoreBinding&, const MockGraph&, Session&) noexcept;
    friend SCO_RESERVED_API Result BeginStartup(Session&, StartupControl&) noexcept;
    friend SCO_RESERVED_API const MockTable* QueryMock(Session&) noexcept;
    friend SCO_RESERVED_API Result TryEnter(Session&, CallPin&) noexcept;
    friend SCO_RESERVED_API Result Close(Session&) noexcept;
    friend SCO_RESERVED_API RetireResult RetireUntil(Session&, std::chrono::steady_clock::time_point) noexcept;
    friend SCO_RESERVED_API Phase Status(Session&) noexcept;
    friend SCO_RESERVED_API void Fault(Session&) noexcept;
    Bootstrap bootstrap_{};
    void* handle_ = nullptr;
    void* secret_ = nullptr;
};

// Thread-bound pin; non-copyable, non-movable and exclusively owned by its
// successful-entry thread (do not access one pin concurrently). Admission/release
// allocate nothing. A wrong-thread release faults and retains the session.
class CallPin {
public:
    CallPin() = default;
    SCO_RESERVED_API ~CallPin();
    CallPin(const CallPin&) = delete;
    CallPin& operator=(const CallPin&) = delete;
    SCO_RESERVED_API explicit operator bool() const noexcept;
private:
    friend class AuthorityImpl;
    void* authority_ = nullptr;
    void* session_ = nullptr;
    std::thread::id thread_{};
    void (*leave_)(void*, CallPin*) noexcept = nullptr;
};

// Explicit, thread-bound startup capability. An unfinished control faults
// and retains the reserved session; it does not grant callback authority.
class StartupControl {
public:
    StartupControl() = default;
    SCO_RESERVED_API ~StartupControl();
    StartupControl(const StartupControl&) = delete;
    StartupControl& operator=(const StartupControl&) = delete;
    explicit operator bool() const noexcept { return active_; }
private:
    friend SCO_RESERVED_API Result BeginStartup(Session&, StartupControl&) noexcept;
    friend SCO_RESERVED_API Result PublishTypedProvider(StartupControl&) noexcept;
    friend SCO_RESERVED_API Result FinishStartup(StartupControl&) noexcept;
    Bootstrap bootstrap_{};
    void* session_ = nullptr;
    void* secret_ = nullptr;
    void* handle_ = nullptr;
    bool active_ = false;
};

SCO_RESERVED_API Result ReserveSession(CoreBinding&, const MockGraph&, Session&) noexcept;
SCO_RESERVED_API Result BeginStartup(Session&, StartupControl&) noexcept;
SCO_RESERVED_API Result PublishTypedProvider(StartupControl&) noexcept;
SCO_RESERVED_API Result FinishStartup(StartupControl&) noexcept;
SCO_RESERVED_API const MockTable* QueryMock(Session&) noexcept;
SCO_RESERVED_API Result TryEnter(Session&, CallPin&) noexcept;
SCO_RESERVED_API Result Close(Session&) noexcept;
// Refused/unknown results never authorize cleanup. Both retained verdicts
// preserve the capsule and process reservation; neither permits restart. The
// supported census covers admitted CallPins, the sealed queue and its one
// owned worker through actual join and TLS destruction. Entry attempts not yet
// admitted, arbitrary threads, foreign callbacks and returned views are outside
// it. A retained result never permits unloading code or caller-owned backing.
SCO_RESERVED_API RetireResult RetireUntil(Session&, std::chrono::steady_clock::time_point) noexcept;
// Invalid handles conservatively report FailedRetained.
SCO_RESERVED_API Phase Status(Session&) noexcept;
SCO_RESERVED_API void Fault(Session&) noexcept;

#ifdef SCO_RESERVED_SESSION_TESTING
namespace testing {
// Hooks are test-only and execute with a live call pin, outside the lock.
void SetBeforeCopyHook(void (*)(void*), void*) noexcept;
void SetBeforeQueuedWorkHook(void (*)(void*), void*) noexcept;
void SetBeforeWorkerStartHook(void (*)(void*), void*) noexcept;
void SetBeforeWorkerPublishHook(void (*)(void*) noexcept, void*) noexcept;
void SetBeforeTerminalCommitHook(void (*)(void*), void*) noexcept;
void SetWorkerTlsExitHook(void (*)(void*), void*) noexcept;
void FailNextReservationAllocation() noexcept;
void FailNextWorkerAllocation() noexcept;
void FailNextWorkerLaunch() noexcept;
void FailQueueBeforeSeal() noexcept;
}
#endif

} // namespace sco::reserved
#undef SCO_RESERVED_API
