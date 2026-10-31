#pragma once
// Private to src/api/. Callout depth (game thread only, so no lock) and the per-module halves
// of Release().
#include "sco/runtime.h"
#include "sco/owned_shutdown.h"
#include <memory>

namespace sco::detail {

bool InCallout();

// Terminal host-session barrier. Start reopens it; shutdown closes it under the
// service registry lock, then cancels all queued work before any unload export.
bool RuntimeStopping();
void SetRuntimeStopping(bool stopped);
void SetRuntimeShutdownOwner(std::shared_ptr<OwnedShutdown> owner);
// A runtime operation is admitted before it touches caller state. The startup
// transition itself is the lease on its owning thread. Raw service-table use
// after a query still requires a caller-held Read for the complete use.
class RuntimeLease {
public:
    RuntimeLease();
    explicit operator bool() const noexcept { return !owner_ || startup_ || static_cast<bool>(read_); }
    bool startup() const noexcept { return startup_; }
private:
    std::shared_ptr<OwnedShutdown> owner_;
    bool startup_;
    OwnedShutdown::Read read_;
};
void StopRuntime();

// Marks a callout (a task, an event callback, a command, a done callback) for its lifetime.
struct CallScope {
    CallScope();
    ~CallScope();
    CallScope(const CallScope&) = delete;
    CallScope& operator=(const CallScope&) = delete;
};

// Runs thunk(ctx) as one callout of `owner` (a CallScope for its lifetime), through the guard
// SetCalloutGuard installed when owner is non-null. False when the call did not complete.
bool Callout(const void* owner, const char* where, TaskFn thunk, void* ctx);

// True once Release(owner) or fault quarantine has started. Checked under each module's own lock, in the same
// critical section that adds the item, so nothing an owner adds can outlive its Release.
bool Released(const void* owner);

// Queues a runtime-internal task fn(ctx) for owner: it runs without the callout guard (fn is
// runtime code that guards the callouts it makes itself). If the task is dropped by
// Release(owner) instead, drop(ctx) runs so the queued state can be freed. drop must not call
// back into the runtime.
Result PostOwned(TaskFn fn, TaskFn drop, void* ctx, const void* owner);

// Invoke() with one addition for callers whose done ctx is heap state (sco::host's C-ABI
// trampoline): whenever done won't run after all, dropCtx(ctx) runs so ctx can be freed:
// Release(owner) dropped the queued off-thread call, the command released owner while it ran,
// or done itself faulted (the callout guard reported false). dropCtx must not call back into the
// runtime. Never called when Invoke returns anything but Ok, or when done ran to completion.
Result InvokeOwned(const char* name, const Arg* args, uint32_t nargs, InvokeDone done, void* ctx,
                   const void* owner, TaskFn dropCtx);

// Each removes the owner's items and returns how many, or -1 when out of memory (nothing
// removed). Called by Release() on the game thread.
long ReleaseTasks(const void* owner);
long ReleaseSubscriptions(const void* owner);
long ReleaseCommands(const void* owner);
long ReleaseServices(const void* owner);
long ReleaseRaw(const void* owner);

// Fault-only quiescence: reject new registrations, withdraw services and scheduled work,
// but never run resource release hooks. The loader keeps the owner and module until exit.
// Any thread; existing raw-table calls are not interrupted or made safe.
// Failure requires a controlled stop: quiescence has not been fully established.
Result QuarantineOwner(const void* owner);
// Runs only host resource release hooks, on the game thread. The fault path calls this
// only after quarantine, and only if no table was handed out.
void ReleaseFaultResources(const void* owner);

// Calls visit(position, slot, gen, cmd, ctx) for each live command in registration order, with
// the registry locked, and returns how many are live. `slot` (< kMaxCommands) is the registry slot
// the command sits in; slots are reused after Release, so `gen` says which registration holds it:
// it differs for every registration, ever. `cmd` is only valid inside the call. visit must not call
// back into the runtime.
using CommandVisitor = void (*)(size_t position, size_t slot, uint64_t gen, const Command& cmd, void* ctx);
size_t VisitCommands(CommandVisitor visit, void* ctx);

// The capability check SetCapabilityCheck installed: true when cap is available (false when no
// check is installed). For modules other than the command registry.
bool CapabilityAvailable(const char* cap);

}  // namespace sco::detail
