# Owned prefab spawning (G4 / #125)

## Boundary and implementation

`game.prefabs` 1.0 is an independent size-prefixed table. Frozen `sco_api` 1.1,
`game.entities` 1.1 and ordinary `spawn.entities` layouts are unchanged.
A canonical `ObjectContainers/.../name.socpak` content-relative path selects
one existing object container; no plugin filesystem read, arbitrary attributes,
raw pointer, network access or supplied engine class is added.

The Windows adapter ports the existing `spawn.prefabs` signature rows and
`ocFilename` attribute writer into the game pack's spawner. Public spawn uses
`StreamingObjectContainer`, same batch, transform and removal primitives as
ordinary entities. No fabricated offsets, duplicate hooks or unrestricted delete.
Native batch failure before spawn releases the created batch where possible;
known IDs from a post-spawn failure remain tracked for ordinary unload cleanup.
SEH cannot guarantee recovery from a partially executing engine spawn with no
returned ID; that case remains an owner-run acceptance blocker.

`kPrefabConfirmed = false`: operation and new row dereferences stay OFF until
the owner verifies streamed spawning and cleanup on the supported Windows build.
Publication is discovery, not capability. Nothing enables it via environment.
This is a tested contract and portable admission implementation, NOT game proof.

## Paths, poses and quotas

* Path length <=511 bytes, excluding NUL. Exact `ObjectContainers/` prefix and
  `.socpak` suffix. ASCII letters/digits, underscore, hyphen, dot, braces, slash.
  Reject traversal/dot/empty components, absolute paths, backslashes, colons,
  encoding, query/fragment, controls and non-ASCII. Preserve spelling exactly.
  This is engine content lookup, not extraction or authorization of OS files.
* Zone is nonzero and currently streamed. Position finite zone-local metres;
  rotation finite nonzero xyzw quaternion, normalized before provider access.
* Quotas checked BEFORE engine mutation: existing shared 1,024 entities and at
  most 128 existing entities owned by this caller. The owner limit deliberately
  counts ordinary entities too, not a second unaccounted prefab ledger.
* No allocations cross ABI. Copy bounded path and pose for the provider call.
  No callbacks into plugins, waits, asynchronous work or reentry.
* Failure zeroes public ID. Success returns a final entity ID, NOT confirmation
  of materialization, preview collisions or successful removal.

## Ownership and cleanup

Record prefab ID in the existing `game.entities` ledger immediately, before it
streams. Mark prefab entries non-prunable: `alive=0` never proves disappearance,
and delayed containers must not silently lose cleanup rights after two minutes.
Only existing explicit despawn/keep/owner cleanup can release those entries.

Use `game.entities.set_transform` after streaming, and `despawn` for cancellation
or removal. Ordinary unload uses the existing delayed removal primitive for
pending and streamed containers. Synthetic provider tests prove admission and
ownership attribution, not engine streaming/removal. Fault-retained native
providers follow the host's restart-required contract, not safe unmapping.

For build placement use `game.owned.retain_build` and `remove_build` while loaded.
Retained placements refuse legacy keep. Legacy keep is an irreversible
relinquishment of all SDK move/removal rights, not a persistence-with-undo API.
No across-reload identity, adoption, arbitrary deletion or player registration.

## Language and evidence

Native C direct route; C++ `Prefabs` guards complete extents, null members and
failed reopen. C# and sandboxed Lua: unsupported. No parity by command fallback.
ABI pins cover C11, C++20, short enums and Windows x64 header compilation.
`tests/test_prefabs.cpp`: canonical/malformed/boundary paths, finite poses, quotas,
provider absence/refusal/fault-ID cleanup ownership, partial/null C++ tails.
G9's actual host/runtime fake-provider fixture proves retained/foreign/seat
refusals and release semantics separately; no combined real engine claim.

## Decision pending owner

Confirm the narrow 511-byte/128-owner limits and retained-only-while-loaded
placement policy. Before enabling: MSVC/SEH build, canonical content resolution,
late materialization, cancellation before first stream, preview/unload cleanup,
post-spawn batch fault and missing-capability refusal. Windows/game not verified.
