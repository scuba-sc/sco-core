#pragma once
// Star Citizen OFFLINE host-source composition, never a plugin service or patch authority.
// Requires SCO_GAME_SC=ON; link sco_game_host. The shell supplies OS admission evidence,
// paths, logging, window/message and renderer adapters. No OS policy is inferred here.
#include "sco/app.h"
#include "sco/owned_shutdown.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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
enum class Phase : uint32_t { Idle, Preparing, Prepared, Activating, Activated, Starting, Running, Stopping, Stopped, Failed };
enum class QuitPolicy : uint32_t { Required, External };
enum class DataCoreHook : uint32_t { Required, External };
enum class ConsumerRetirement : uint32_t { Quiesced, Retained };
// Cooperative reader/worker progress, distinct from HostReport::result and
// DependencyDisposition. Drained never means app cleanup or native retirement.
enum class DrainDisposition : uint32_t { NotAdopted, Open, Requested, InProgress, Drained, Retained, Unknown };
struct Snapshot {
    Phase phase = Phase::Idle;
    bool signaturesResolved = false;
    bool quitHookInstalled = false;
    bool dataCoreActive = false;
};
struct Options {
    uint32_t size = sizeof(Options); // C++ source contract, not frozen sco_api or a wire ABI
    Admission (*inspect)() = nullptr; // synchronous, thread-safe, non-reentrant OS evidence; exceptions refuse
    const Image* image = nullptr;    // readable through Prepare
    std::shared_ptr<const void> imageOwner; // owns image bytes and mapped code
    std::shared_ptr<const void> resolverOwner; // owns resolver provider code/state
    QuitPolicy quit = QuitPolicy::Required; // External only when shell supplies BeforeQuit
    uint32_t tickIntervalMs = 100;    // 0 = every shell pass; <= INT32_MAX
    // The synthetic external transport may start DataCore during Activate;
    // transforms are refused until Running. Early engine loading is unsupported.
    DataCoreHook dataCoreHook = DataCoreHook::Required;
    uint64_t maxDataCoreBytes = 512ull << 20; // bounded base read for Data/Game2.dcb
    app::Platform platform;          // image/registerSignatures MUST be null: owned here
    // Native live reads are OFF by default. The shell must keep engine execution
    // quiesced before Activate and until it returns; late activation is insufficient.
    bool nativeDataCoreReads = false;
    bool preEngineQuiesced = false;
    // 0 means allocator-limited; otherwise registry, graph and snapshot allocations
    // share this byte budget, including snapshots retained by readers.
    size_t signatureRegistryBudgetBytes = 0;
    // Legacy callback retained for migration; supplying it refuses activation.
    bool (*earlyDataRequirement)(const char* name, const SignatureSnapshot& snapshot, void* context) = nullptr;
    void* earlyDataContext = nullptr;
    // DEPRECATED: legacy assertion, not invoked or accepted as drain proof.
    // Retained for source migration; shutdownOwner is required instead.
    ConsumerRetirement (*retireConsumer)(void* context) = nullptr;
    void* consumerContext = nullptr;
    // Required ownership evidence. Supported only with external quit/data hooks,
    // no native game services and no legacy plugins/builtins/script/host adapters.
    // Those consumers can bypass Read/worker admission and remain unsupported.
    std::shared_ptr<OwnedShutdown> shutdownOwner;
    uint32_t drainTimeoutMs = 1000; // 1..60000; timeout is terminal, never readiness
};

// Additive C++ host-source reporting, separate from the frozen plugin ABI.
// HostReport is returned by value and its layout changed: rebuild C++ consumers
// with the same Core. These reports never authorize cleanup, unload, native
// activation or restart. The frozen plugin C ABI is unchanged.
enum class DependencyDisposition : uint32_t { NeverExposed, ProcessRetained };
struct HostReport {
    Result result = Result::Unavailable;
    Phase phase = Phase::Idle;
    uint64_t sessionId = 0; // process-scoped; nonzero only for an adopted capsule
    DependencyDisposition dependencies = DependencyDisposition::NeverExposed;
    DrainDisposition drain = DrainDisposition::NotAdopted;
};

// Build privately before calling PrepareDetailed. Owns Options, its Image
// descriptor and host-version string. backing must ACTUALLY own every referenced
// image byte and accepted context through process exit; resolver/inspector code
// is strongly pinned or part of the audited process-resident executable.
// A borrowed pointer, no-op deleter or merely non-null token is not ownership.
// This is a trusted, audited host composition contract, not automatic ownership
// validation or support for arbitrary providers/modules. Construction may throw;
// it performs no core mutation or callback. Do not copy/move this object because
// its Options point into its own immutable descriptor/string storage.
class HostCapsule final {
public:
    HostCapsule(const Options&, std::shared_ptr<const void> backing);
    HostCapsule(const HostCapsule&) = delete;
    HostCapsule& operator=(const HostCapsule&) = delete;
    HostCapsule(HostCapsule&&) = delete;
    HostCapsule& operator=(HostCapsule&&) = delete;
    const Options& configuration() const noexcept { return options_; }
    bool hasBacking() const noexcept { return static_cast<bool>(backing_); }
private:
    std::shared_ptr<const void> backing_;
    Options options_;
    Image image_{};
    std::string hostVersion_;
};

// Fresh, serialized non-native composition only: external quit/data hooks; no
// DataCore, storage root, native services, plugins/scripts, private session,
// host adapters or legacy callback assertions. The inspector and code must be
// process-pinned by the audited capsule. Every ordinary user holds its owner's
// Read through the final output copy. Only audited capsule-owned copied facades
// are qualified: app/global raw service tables, views and foreign workers are
// not enrolled by this API. This is not successful app retirement.
// Adopt/root before existing Prepare can copy Options or expose dependencies.
// Adoption is terminal even on failure. A later refusal/exception/Stop == Ok
// never lowers ProcessRetained. NeverExposed applies only to a fresh pre-adoption state or refusal;
// it cannot discharge aliases held by a prior invocation/session.
HostReport PrepareDetailed(const std::shared_ptr<const HostCapsule>&) noexcept;
HostReport ActivateDetailed() noexcept;
HostReport StartDetailed() noexcept;
HostReport StopDetailed() noexcept;
// Close the already-adopted capsule's owner without waiting for admitted work.
// Callable from a reader/worker/startup callout, including while the host control
// gate is held. Does not run an inspector, cleanup, app Stop or phase transition.
// Ok confirms admission closure and a cooperative stop request only; phase is an
// observation and may still be Running. It never means drained or cleaned up.
// Unavailable before adoption does not queue a request for a future capsule;
// Failed means closure is unproven. Every adopted result stays ProcessRetained.
// Later, outside every enrolled obligation, use the same capsule owner's
// drain_until(deadline). The host's existing failed-retirement policy remains.
// This takes the owner's short mutex; it is not lock-free or loader-lock safe.
HostReport RequestStopDetailed() noexcept;
// Busy status returns Unavailable with conservative retained data; it is not
// a completed control result/adoption decision and does not latch exposure.
// Busy means the initial gate observation was held. Overlapping status calls
// may return an older observation after a newer one; no return-order monotonicity
// is promised. Only the completed PrepareDetailed result classifies its input.
HostReport StatusDetailed() noexcept;
// Prior legacy control use is conservatively ProcessRetained with id 0: its
// backing is caller-owned, not retroactively adopted. A busy, not-yet-adopted
// control frame also reports conservative ProcessRetained with id 0. These
// reports do not assert that new input was adopted. Detailed Prepare refuses
// that composition. Once adopted, legacy Prepare cannot replace the capsule;
// legacy Activate/Start/Stop/Tick share the detailed control gate and floor.
// Serialized host entries, outside loader lock/runtime callouts. Prepare may precede the game
// thread; Start binds the game thread via app::Start. Unknown/online/EAC/unblocked-network/wrong
// target refuses BEFORE registration/resolution, hooks, service publication or plugin callbacks.
// All rows resolve before any later engine mutation. Missing rows remain unavailable, not guessed.
// Options are copied; pointed-to Platform inputs retain app.h's lifetime contract.
// Resolved addresses and installed hooks borrow the image/module and resolver provider code.
// Keep those valid through all consumers and hooks; a retained snapshot is not an image lease.
Result Prepare(const Options& options);
// Native mutation requires a durable platform reader barrier; refusal is retryable.
Result Activate();
// Start activates a prepared candidate for compatibility, then starts it.
Result Start();
// Shell invokes from its game-thread message/tick adapter; first pass is immediate.
// Unsigned elapsed time handles clock wrap. Rechecks admission BEFORE cadence or callbacks.
// Lost admission stops the host without executing queued work. No fallback feature ticks.
Result Tick(uint32_t nowMs);
// Game thread. Stop closes admission and attempts the owned drain, then returns
// Failed and retains the app, registry, image, provider code and dependent backing.
// A failure-atomic cleanup transaction is not yet established. No restart from Failed.
Result Stop();
// Legacy synchronous game-thread stop request from the engine quit detour. It can
// run the configured owner drain on that hook's stack and does not call app::Stop,
// dispatch game.exit, or prove dependent-provider withdrawal. No fast-shutdown
// field/vtable reads; normal and fast quit share this entry.
Result BeforeQuit();
// Immutable scalar observation; no provider/table callbacks. May read on any thread.
Snapshot Status();
// Host-only worker-safe fresh predicate, NOT exposed through plugin discovery.
// True only during Start/Running and fresh inspect() positively clears every G1
// condition. Inspector/backing state must remain process-retained; no plugin code.
// A fresh observation only; every owned cached/native use must additionally hold
// its owner's Read lease for the full operation. This bool is not a barrier.
bool FreshAdmissionAllowed() noexcept;

#if defined(_WIN32)
// Read-only inspection of Windows Firewall policy (INetFwPolicy2 COM API).
// Verifies whether an active outbound block rule with ruleName exists for the current or specified image.
// Never modifies firewall rules, never requires elevation, fails closed (returns Check::Unknown on any COM/policy failure).
Check InspectWindowsFirewallRule(const wchar_t* ruleName, const wchar_t* imagePath = nullptr) noexcept;
#endif

} // namespace sco::game::host
