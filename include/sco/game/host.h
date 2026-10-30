#pragma once
// Star Citizen OFFLINE host-source composition, never a plugin service or patch authority.
// Requires SCO_GAME_SC=ON; link sco_game_host. The shell supplies OS admission evidence,
// paths, logging, window/message and renderer adapters. No OS policy is inferred here.
#include "sco/app.h"
#include <cstddef>
#include <cstdint>

namespace sco::game::host {
enum class Session : uint32_t { Unknown, Offline, Online };
// Clear means positively checked: correct target, no anti-cheat, blocked official network.
// A failed OS inspection MUST yield Unknown, never Clear. Present always refuses.
enum class Check : uint32_t { Unknown, Clear, Present };
struct Admission {
    Session session = Session::Unknown;
    Check antiCheat = Check::Unknown;
    Check officialNetwork = Check::Unknown;
    Check targetProcess = Check::Unknown;
};
enum class Phase : uint32_t { Idle, Preparing, Prepared, Starting, Running, Stopping, Stopped, Failed };
enum class QuitPolicy : uint32_t { Required, External };
enum class DataCoreHook : uint32_t { Required, External };
enum class ConsumerRetirement : uint32_t { Quiesced, Retained };
struct Snapshot {
    Phase phase = Phase::Idle;
    bool signaturesResolved = false;
    bool quitHookInstalled = false;
    bool dataCoreActive = false;
};
struct Options {
    uint32_t size = sizeof(Options); // C++ source contract, not frozen sco_api or a wire ABI
    Admission (*inspect)() = nullptr; // synchronous, thread-safe, non-reentrant OS evidence; exceptions refuse
    const Image* image = nullptr;    // Image object and original bytes readable through Prepare
    QuitPolicy quit = QuitPolicy::Required; // External only when shell supplies BeforeQuit
    uint32_t tickIntervalMs = 100;    // 0 = every shell pass; <= INT32_MAX
    // platform.dataCore starts the service in Prepare, before an early engine load.
    // Requires dataRoot and no builtins (early discovery runs no plugin code).
    DataCoreHook dataCoreHook = DataCoreHook::Required;
    uint64_t maxDataCoreBytes = 512ull << 20; // bounded base read for Data/Game2.dcb
    app::Platform platform;          // image/registerSignatures MUST be null: owned here
    // Native live reads are OFF by default. The shell must keep engine execution
    // quiesced before Prepare and until it returns; late Prepare is insufficient.
    bool nativeDataCoreReads = false;
    bool preEngineQuiesced = false;
    // 0 means allocator-limited; otherwise registry, graph and snapshot allocations
    // share this byte budget, including snapshots retained by readers.
    size_t signatureRegistryBudgetBytes = 0;
    // Optional early DataCore requirement check. Null or an exception means not
    // available. Runs synchronously during Prepare, possibly before the game thread,
    // after admission and against the installed frozen snapshot; it must be
    // side-effect-free and not retain the name or snapshot references. The callback
    // and context must remain valid through Prepare.
    bool (*earlyDataRequirement)(const char* name, const SignatureSnapshot& snapshot, void* context) = nullptr;
    void* earlyDataContext = nullptr;
    // Called at most once after core admission closes and before any pak, app,
    // plugin or dependent service teardown. A failed Prepare can call it on the
    // Prepare thread; Stop calls it on the game thread. Quiesced asserts a durable gate:
    // product admission is closed, entered product and game-service/native readers
    // have drained, and asynchronous product/engine callbacks or service readers
    // cannot enter through all dependent cleanup. It must
    // synchronize instruction patching with executing threads and safely allow any
    // required cleanup-thread reentry without reopening asynchronous admission.
    // Exempt the current Stop/quit control frame from its drain to avoid self-deadlock;
    // keep its image, trampoline and provider code alive until it exits, and through
    // process exit wherever retained hooks need them. Do not re-enter host lifecycle
    // calls. A point-in-time observation or RemoveAll alone is not this guarantee.
    // Return Retained when no durable proof exists. Null, Retained or an exception refuses
    // teardown and retains the registry and code. Callback/context outlive Stop.
    ConsumerRetirement (*retireConsumer)(void* context) = nullptr;
    void* consumerContext = nullptr;
};
// Serialized host entries, outside loader lock/runtime callouts. Prepare may precede the game
// thread; Start binds the game thread via app::Start. Unknown/online/EAC/unblocked-network/wrong
// target refuses BEFORE registration/resolution, hooks, service publication or plugin callbacks.
// All rows resolve before any later engine mutation. Missing rows remain unavailable, not guessed.
// Options are copied; pointed-to Platform inputs retain app.h's lifetime contract.
// Resolved addresses and installed hooks borrow the image/module and resolver provider code.
// Keep those valid through all consumers and hooks; a retained snapshot is not an image lease.
Result Prepare(const Options& options);
// Re-inspects admission before any publication. Prepared only; no feature fallback tick.
Result Start();
// Shell invokes from its game-thread message/tick adapter; first pass is immediate.
// Unsigned elapsed time handles clock wrap. Rechecks admission BEFORE cadence or callbacks.
// Lost admission stops the host without executing queued work. No fallback feature ticks.
Result Tick(uint32_t nowMs);
// Game thread. Stop cancels tasks through app::Stop; repeated stop is safe. If teardown
// cannot complete, the host fails and retains its registry for any surviving consumers.
// A process-retained quit or native DataCore detour means Stop returns Failed and retains
// the registry through process exit, even after services stop. The shell must keep the
// borrowed image and provider code valid through that same period.
// Product-owned readers require the retireConsumer transition above; attempted game
// services and host-owned native detours also require Platform::gameReadersQuiesced
// to hold its native-reader barrier before teardown.
// No restart from Stopped/Failed: process-lifetime hook ownership requires a fresh process.
Result Stop();
// Synchronous game-thread quit admission: stop BEFORE the engine may terminate the process.
// No fast-shutdown field/vtable reads; normal and fast quit share this entry.
Result BeforeQuit();
// Immutable scalar observation; no provider/table callbacks. May read on any thread.
Snapshot Status();
// Host-only worker-safe fresh predicate, NOT exposed through plugin discovery.
// True only during Start/Running and fresh inspect() positively clears every G1
// condition. Inspector/backing state must remain process-retained; no plugin code.
bool FreshAdmissionAllowed() noexcept;

#if defined(_WIN32)
// Read-only inspection of Windows Firewall policy (INetFwPolicy2 COM API).
// Verifies whether an active outbound block rule with ruleName exists for the current or specified image.
// Never modifies firewall rules, never requires elevation, fails closed (returns Check::Unknown on any COM/policy failure).
Check InspectWindowsFirewallRule(const wchar_t* ruleName, const wchar_t* imagePath = nullptr) noexcept;
#endif

} // namespace sco::game::host
