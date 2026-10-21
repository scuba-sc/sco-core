# Live DataCore host backend

C++/C#/Lua typed wrappers are unsupported; C table access is supported.

`game.datacore_live` 1.0 provides bounded scalar-pool access through a copied
host backend. The Windows native read adapter is OFF by default. Without verified callbacks,
fresh offline admission and a live pool/session, operations return unavailable.

Calls require the game thread and a live plugin owner. Output records require
their full declared extent. The backend owns pool bounds and layout validation.
Writes additionally require a separate host opt-in, OFF by default.

Start cannot replace a live backend. Stop withdraws the service, clears callbacks
and write permission. A callback exception or native fault latches the provider
unavailable until process restart. Synthetic pool fixtures are not native engine
evidence. Not run in game.

## Native read composition

The offline host may set `Options::nativeDataCoreReads` only when its shell
enforces `Options::preEngineQuiesced`: engine execution is held quiescent before
`Prepare` starts and until every required hook is installed and `Prepare` returns.
Calling `Prepare` late or setting the flag without that guarantee is unsupported.
Native reads also require the normal positive fresh offline admission, game
services, DataCore preparation and the SDK-owned loader hook. No second loader
detour is installed. Missing locators or lifecycle hooks leave access unavailable.

The process-retained lifecycle gate excludes reads during loading, reset and
destruction, and excludes those lifecycle calls while a read validates and copies
native bytes. Native read sections make no engine or plugin calls. Reentrant
read-to-lifecycle upgrades violate this invariant and terminate rather than skip
native teardown or continue without exclusion. Stop withdraws service access;
installed reset/destructor synchronization remains retained for process lifetime.
The deleting wrapper is also covered through the complete owner free. Any native
reset or destruction retires native read admission until process restart, before
the original operation runs; a still-published owner cannot reopen access during
teardown. Failed loader-detour removal keeps ownership retryable and host Stop
returns failure instead of reporting completed teardown.
Loader disable takes lifecycle exclusion before its state lock, draining an
active loader call before host service withdrawal.

Scalar statistics describe the current counted allocation extent; capacity equals
the populated count because the serialized scalar buffers are allocated to that
count. Offsets and lengths are bytes and may span elements within that extent.

The owner reports a finished load attempt, not a successful one, so the native reads also refuse a
missing header or pool; a load that failed late can still leave a pool only partly filled
(F-4.10.197.2878-051). Pool element widths are verified on the build; the type names follow the
game's own field-type names. Not run in game.
STRING exposes the serialized value-text blob itself, including embedded NUL and
non-ASCII bytes: element size is one and count/capacity are blob byte length. It
does not expose native string pointers or the separate string-offset index table.

Record GUIDs use the maintained parser's raw 16-byte representation. Duplicate
keys fail rather than selecting an arbitrary record. `struct_type` is the
serialized struct-definition index; `field_count` includes inherited fields with
validated parent chains; `pool_offset` is the matching record's byte offset within
the serialized record table, not a native address or per-type instance index.

Native writes always return unavailable, even if the generic host write opt-in is
set. Native read fixtures and signature checks do not establish in-game behavior,
provenance approval or SDK release readiness.
