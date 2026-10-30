# Entities rows

## Copied lookup extension1.2

1.2 appends class registry pages, exact class lookup, debug-grade instance-name
lookup and typed component presence. Existing1.0/1.1 members retain their offsets.
The new native C++ methods guard complete member extents on older providers.
1.2 tails: C# and Lua unsupported; existing earlier language routes are unchanged.

Registry snapshots contain at most64complete copied classes, not just classes
observed through class_of. Provider overflow must refuse TOO_MANY rather than
claim an observed subset is complete. Pages sort by host-issued class_id; epoch
pins later pages and expires on registry/tick/session changes. Class names are
exact case-sensitive printable ASCII up to95bytes. Missing/stale records return
NOT_FOUND. Duplicate IDs/names or malformed later records publish no partial page.

Instance names are debug-grade ASCII case-insensitive; ambiguous matches return
FAILED instead of promising a stable traversal winner. Component lookup accepts
only ACTOR and INVENTORY discriminators and copies entity/kind/generation presence.
It is not an arbitrary component-memory reader or a cross-call native handle.
Requery before using a streaming observation; generation is not a permanent key.

Production lookup capability is OFF: the table publishes unbound callbacks that
refuse UNAVAILABLE until independently grounded native admission exists. No new
native row, offset or hook is invented. `tests/test_entity_lookup.cpp` exercises
bound synthetic providers, epoch mismatch, duplicate/malformed snapshots, name
folding/ambiguity, typed presence, future tails, physical short tables and callback
stop invalidation. C/C++ ABI pins retain old offsets. Not run in game.

Which capabilities are on, and how far each has been checked: [status](status.md) (`game.entities.query_radius` and `.watch` are off until an in-game run).

The addresses `game.entities` uses, from Appendix B of [game world spikes](../design/game-world-spikes.md) (B3 and B9). Rows are in [`src/game/entities_sigs.cpp`](../../src/game/entities_sigs.cpp), the accessor constants and capability groups in [`include/sco/game/entities.h`](../../include/sco/game/entities.h). Checked against 4.10.196.36804.

| Row | Finds | Checks |
|---|---|---|
| `entities.dump` | The console dump `EntitySystemDump` (no name filter), by the unique pattern `48 8B 0D ?? ?? ?? ?? 48 8D 55 E7 48 89 45 F7 41 80 E0 01 48 8D 45 B7` | the lambda's `lea` at +0x3B, the ForEach call at +0x46, the entity count read at +0x24D |
| `entities.index` | The entity index global (RIP operand of the `mov rcx` at +0x00) | outside the image fails |
| `entities.for_each` | The game's "for each entity" function (the call at +0x46 of the dump) | the call and its target in `.text` |
| `entities.class_site` | The dump's lambda `void (ctx, entity handle)`, from the `lea` at +0x3B | its prologue and handle mask, the class name read (entity slot `0x20`, then that class's slot `0x18`) at +0x28, entity slot `0x398` at +0x6E and the id read (entity slot `0x08`) at +0xAB: the offsets `entities.cpp` relies on |
| `entities.spawn_sinks` | `CEntitySystem::CallOnSpawnSinks`: its label's `lea` (count 1) at +0x17 of `FunctionStart` | the prologue, the sink list at +0x70, the sink call at +0xF0 |
| `entities.delete_entity` | `CEntitySystem::DeleteEntity`: both of its labels (count 1 each, +0x6C and +0x16B) lead to the same `FunctionStart` | the prologue, +0x26, the handle mask at +0xBB |

Capability groups (`sco::game::entities::Capabilities`): `entities.class_name` (class_of), `entities.enumerate` (query_radius), `entities.stream_hooks` (watch). `keep` (1.1) reads no game memory and needs no row of its own: it is the `game.entities.spawn` capability's, like `despawn`. What the rows can't pin, and the constants that stay `false` until an in-game run: [game services](../game-services.md).

## Native point-lookup seam (K-41)

`InstallNativeEntityLookup` takes the class registry instance, an exact class-name lookup and a
registry teardown generation, and installs nothing unless all three are present. The receiver
lifetime is the missing piece: the registry can be torn down and the class records it answers are
only valid through the copy, so production resolves nothing and refuses with the first missing
part. The capability stays OFF either way.

The seam wires `name` only and leaves `classes` and `component` null: it is a point lookup, never a
snapshot provider, and it cannot be mistaken for one. A registry generation that moved since
install means the registry was torn down, so the seam answers `SCO_UNAVAILABLE` rather than handing
out a stale class id; a host-issued id of zero is refused as `SCO_FAILED` because zero is not a
class. Not grounded, not enabled, not run in game.
