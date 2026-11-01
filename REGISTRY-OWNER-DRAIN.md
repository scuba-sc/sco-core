# Owned shutdown candidate — release hold remains

Base: 81942c342836925d7c1227bc5082fdc4d1ba9fc8. Isolated worktree only.

## Feasibility decision and implemented scope

Owner selected proven drain, not fail-stop. No termination policy was added.
A core-only shared_ptr or retained DLL does not own arbitrary native workers,
cached peer tables, borrowed host contexts, engine execution or CRT destruction.
Consequently game::host::Activate now refuses legacy native game services/hooks,
plugin loading, builtins, scripts, host service/menu adapters and capability/early
callbacks. Supplying an OwnedShutdown object does not waive those refusals.
The old retireConsumer and gameReadersQuiesced assertions do not authorize activation.
Existing product configurations therefore stay unavailable; this is intentional.

The finite supported synthetic host-source path has external quit/data hooks,
plugins disabled, no builtins/native game services/adapters, and an owner-held
OwnedShutdown. Every external cached call must hold its Read for the complete
call; every worker must be started in this owner (64 slots). There is no automatic
interposition on arbitrary native function pointers: an embedding product that
cannot establish that complete census is UNSUPPORTED and must not activate.
This is an explicit integration contract, not proof that an arbitrary caller has
obeyed it. The core cannot detect a hidden unmanaged thread or an unwrapped call.

OwnedShutdown closes reader and worker admission atomically, requests stop, waits
for all reads, owns worker joins and only then publishes readiness. A worker body
finishing is not join proof: a separate owned joiner covers thread-local teardown
while the control thread obeys its deadline. Missing owner, timeout, worker throw
or explicit fault never publishes ready; timeout is terminal. No detached thread,
kill, abort or forced termination was introduced. Drain deadlines do not bound
scheduling latency or mutex starvation; callbacks are deliberately not run as drain
proof. A timed-out owner's object and borrowed backing MUST remain live until its
workers actually exit. Its destructor joins and can block; there is no claim of
bounded destruction or safe normal process exit with a permanently stuck worker.

Host Stop drains before pak/data changes; data input reset moved after successful
app retirement. App rechecks owned readiness after game.exit and unload. UnloadAll
now returns false before any teardown for existing crashed/faulted/quarantined
code, and stops remaining peers on unload/release faults. App propagates failure
before dependent backing teardown. Previously unloaded providers cannot be restored
if later cleanup faults: this is not an intact-closure/recovery guarantee for legacy
consumers. The supported host path excludes those legacy consumers.

## Tests

RED: standalone OwnedShutdown initially failed compilation on missing header;
host owned-missing failed three assertions (activation/publication not refused);
app exit invalidation failed because Stop incorrectly returned Ok. Loader unload
and release fault regressions were red before their fixes.

GREEN: targeted CTest owned_shutdown, owned_app_stop, game_host_owned_missing,
game_host_owned_legacy, game_host_owned_good, game_host_owned_timeout, test_plugins
passed 7/7; final rerun including the migrated legacy game_host refusal case passed
8/8. Standalone OwnedShutdown passed AppleClang ASan+UBSan with leak detection
disabled. Synthetic cached read admission, worker stop and join, TLS join timeout,
terminal timeout/no false ready, worker throw and post-drain fault invalidation are
exercised. Legacy game_host scenarios now assert activation refusal, not their old
successful lifecycle behavior. Broader old data/cvar host fixtures are not migrated.

## Remaining product barriers (not solved or enabled)

- Complete native entry/target execution exclusion (including engine detours and
  current quit continuation lifetime), and cached-table/read census in the product.
- Migration or refusal of raw-pointer SDK consumers, native/builtin/script workers,
  borrowed Menu/HostService contexts and owner release hooks; no unowned bypass.
- Windows/MSVC/SEH integration and real native target exclusion. No Windows access
  or game execution occurred.
- Fault-atomic retirement for arbitrary legacy unload/release callbacks. A fault
  can destroy its own state, corrupt peers or occur after earlier providers retire.
- Existing loader off-thread containment std::abort paths are unchanged, not an
  approved fallback. Strict host activation prevents loading those legacy providers;
  generic app users retain that pre-existing barrier and are not certified here.
- Packaging/provenance registration for the new host header/tests, full suite and
  old host fixtures. No release/package/ABI compatibility claim.

No process termination implementation, owner checkout changes, push, merge or game.

## HOLD — independent review verdict (do not merge)

Independent review found blocking defects; this branch is preserved only as
reference for the next design pass:

- A 5 ms drain deadline measured 275 ms; callable re-entry can deadlock.
- Cleanup failures can leave ownership marked "ready".
- 14 of 246 tests regress against a clean baseline, including an app segfault.
- `void -> bool` signature change breaks typed void-return function-pointer
  consumers, and callers can ignore the new failure result.
- `sdk/components.json` omits the new header and tests, so source archives
  would ship without them.

Owner decision: require proven reader/worker shutdown; keep unsafe native
activation refused; no fail-stop process termination; a success-only callback
is not evidence.
