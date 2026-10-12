# Content access: G14 audit and closure

## Audit first

Audited baseline `ae454a3` (issue #135). Graph discovery was unavailable and the
available codebase graph had no indexed projects; source inspection was used.

| Existing surface | Evidence | Consumer route |
|---|---|---|
| Frozen `sco_api` 1.1 | `include/sco_api.h`, commands and service lookup members | Command enumeration/invocation and service discovery, not content enumeration/read. Unchanged. |
| Command implementations | `src/app/sco_app.cpp`, `src/api/sco_commands.cpp`, `src/api/sco_raw.cpp`, `src/host/sco_host.cpp` | The app registers `sco.reload`; the registries accept plugin handlers. No built-in content handler exists. |
| Private content index | `include/sco/plugins.h`, `src/plugins/content.cpp`, `include/sco/app.h` | `ContentIndex::Items/Find/FromPlugin` and `app::Content` return C++ records with filesystem paths. Host-source access, not a published plugin SDK route. |
| Public pack prose/checker | `sdk/docs/data-packs.md`, `sdk/tools/sco-plugin-check.c` | Indexing/checking describes discovery; it does not give an independently compiled plugin a read API. |
| Existing DataCore | `include/sco_datacore.h`, `src/datacore/service.cpp` | Existing game-pack patch pipeline; **not a missing API** and not replaced here. |

**Verdict:** enumeration/read of feature lists and XML scripts is absent from the
public SDK. The conditional G14 gate is real. A game-neutral, read-only service
is sufficient; no game provider, transport, command broker or base ABI change.

## Decisions

- Publish `sco.content` 1.0, a separate size-prefixed C service, with `item` and
  `read`. Only indexed `lists/*.txt` and `scripts/**/*.xml` are exposed. Mission,
  rules and DataCore consumption retain their existing host/game pipelines.
- Copy bytes at startup **before any consumer code loads**. Startup packs depending on code providers are explicitly
  refused; data-pack refusal propagates to data dependents. No public callback
  opens a file, accepts a filesystem path or exposes an absolute path. Script
  bytes are user-authored data, not permission to execute code.
- Enumerate by kind and zero-based ordinal, in index order: plugin discovery
  order, then relative filename byte order. Duplicate names remain distinct;
  consumer policy determines merge/override semantics. Base product files are
  neither guessed nor silently imported into packs.
- Copy records into caller-owned size-prefixed buffers. Opaque 64-bit item IDs
  are never reused within the process; prior-start IDs fail after restart.
  Reads use an exact byte-size handshake (no added NUL, embedded NUL allowed).
- Bounds: 4096 snapshot items, 1 MiB/file, 16 MiB/snapshot, 1023 relative-name bytes, 63 pack-ID bytes.
  A pack whose supported content cannot be safely copied contributes nothing and
  is refused as a whole; the private index is reconciled after refusal. Pack
  file-count, disabled, ordering and discovery rules stay in `ContentIndex`.
- Reject link/reparse-point path components while opening snapshot files; read
  only regular files beneath the discovered pack. Failure refuses the pack rather
  than falling back to unguarded reads. Concurrent file writes are not an atomic
  filesystem transaction: install/update packs while the host is stopped.
- All service calls and host lifecycle operations are game-thread-only. Validate
  live plugin handles and terminal runtime admission. Stable table storage lasts
  for the process; stopped calls return unavailable. Caller buffers are borrowed
  only for a call. No subscriptions, ownership transfers or reload support.
- C plus guarded C++ (`scosdk/content.hpp`) in this slice; no C#/Lua binding is claimed or added for
  parity. Consumers must guard complete callback extents and non-null pointers,
  including the frozen root's `query_service`, and clear stale lookup state.

## Acceptance and exclusions

A synthetic, kernel-only targeted test exercises discovery, ordering/conflicts,
whole-pack refusal, startup availability, bounded snapshot reads, disabled packs,
wrong-thread/invalid-owner calls, size/null guards, stale IDs and stop/restart.
Layout pins and catalogue/source packaging describe the new route. No extracted
game data, real game IDs, allowlist changes or network/game runtime tests.

Step 2C's product quantum-pack move, legacy-list migration and CryPak/DataCore
activation are downstream integration work, not performed by this sco-core G14
issue. Existing DataCore atomic/conflict behavior is not reimplemented. Windows
MSVC/native file handling and owner-local game acceptance remain unverified.

## Decision pending owner

The narrow limits and list/script-only exposure are reversible policy defaults
for 1.0. Wider content kinds or language adapters require separately justified
consumer work; this change does not authorize them.
