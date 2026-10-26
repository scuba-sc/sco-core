# Data packs

A data pack adds content files and nothing else. It never runs code, so it is the safest kind of plugin to install.

```
data\plugins\travel_pack\
  plugin.ini          kind = data, no entry
  lists\locations.txt
```

## What a pack can carry

| Folder | Files | Kind in the content index |
|---|---|---|
| `missions\` | `*.cwmission` | `mission` |
| `missions\` | `<id>.scom`, id of 1..100 lowercase ASCII letters, digits or underscores; no subfolders | `script` (hosts after SDK 2.4 RC1) |
| `rules\` | `*.rules` | `rules` |
| `scripts\` | `*.xml`, up to 16 subfolder levels below `scripts\`; deeper content is ignored | `script` |
| `lists\` | `*.txt` | `list` |
| `datacore\` | `*.toml` | `datacore`: changes to game data ([below](#game-data-datacore)) |

The `.scom` namespace is exact and case-sensitive; other extensions match in any case. Everything else in the folder is ignored. Links (symlinks, junctions) are skipped, so a pack can't point outside its own folder. A pack holds at most 4096 content files; more refuses the whole pack.

## How features use it

The host privately indexes enabled packs. New hosts expose **`sco.content` 1.0**
(`sco_content.h`; C++ `scosdk/content.hpp`) for public **list/script** enumeration
and reads; SDK 2.0.0's existing release predates this addition. Discover it using
`sco_api` 1.1 `query_service`, guarding the complete pointer extent and a non-null
callback. `requires = sco.content` makes absence an explicit discovery refusal.
The private `ContentIndex` and `app::Content()` are host-source APIs, not plugin
SDK headers. Existing DataCore methods are not replaced by this service.

`item(self, kind, ordinal, &record)` uses zero-based ordinals within
`SCO_CONTENT_LIST` or `SCO_CONTENT_SCRIPT`. Set `record.size = sizeof(record)`.
It copies pack ID, pack-relative name and an opaque item ID. `SCO_NOT_FOUND`
ends enumeration. Order is plugin discovery order, then relative filename byte
order. Duplicate names remain distinct items; the feature chooses its merge or
conflict policy rather than assuming one winner.

`read(self, record.id, bytes, &size)` copies exact snapshot bytes: capacity in,
bytes written on success, bytes needed with `SCO_TOO_MANY`. A NULL output with
zero capacity asks the size; empty files return OK/0. **No NUL is appended**;
embedded NULs are allowed. No callback opens a file or accepts a path. Strings
in records are copied UTF-8/NUL-terminated, never absolute host paths.

The snapshot is prepared before **any** plugin load callback. All methods require
the game thread and a live plugin handle; wrong thread returns `SCO_WRONG_THREAD`.
Stopped/terminal-shutdown calls return `SCO_UNAVAILABLE`. Host restart invalidates
old IDs. Packs and DataCore changes require restart, not `sco.reload`; install or
update packs while the host is stopped. Bounds are 4096 snapshot items, 1 MiB per
file, 16 MiB total bytes and 1023 relative-name bytes. Unsafe/unreadable supported
files or a quota refuse the entire pack, and data dependents are also refused.
Startup packs may depend on host capabilities/services or other data packs, but
not code providers that have not loaded yet. Files are opened without following
link/reparse-point components beneath the host-configured pack root; ancestors
of that root remain trusted host installation policy. Concurrent writes are not
an atomic filesystem transaction.

The [native C content reader](../examples/content_reader/README.md) and
[G14 audit/design](../../docs/design/content-access.md) explain the boundary.
Script data is not permission to execute scripts. Hosts after SDK 2.4 RC1 also
expose owned `missions/<id>.scom` declarations as `SCO_CONTENT_SCRIPT`, allowing
the product contracts consumer to read its declared format through this public
table. Published RC1 does not discover those files. The host copies opaque bytes;
the consumer validates the declaration and still needs an available mission
ingest provider. Native mission capability enablement is separate. Private
`.cwmission`/rules exposure, C#/Lua bindings and automatic import of legacy
product base lists are not provided.

Each feature decides which files it reads and in what format. A list file uses the format of the sc-offline file it extends: [`examples/travel_pack/lists/locations.txt`](../examples/travel_pack/lists/locations.txt) uses the `system | name | entity | radius` lines of sc-offline's `data\locations.txt`. Which features read pack content, and from which names, is listed in sc-offline's docs as each feature gains support.

## Game data: `datacore\`

A pack can change the game's own data (`Data\Game2.dcb`, the DataCore database) with `.toml` files in `datacore\`: set a record's field, add an object cloned from another and point a field at it, append to an array, add a new record cloned from an existing one (`[[record]]`). Records and fields are named, not located by byte offset, so a pack keeps working across game patches as long as the records and fields it names still exist. [`examples/quantum_pack`](../examples/quantum_pack/datacore/eos.toml) is a complete example:

```toml
format = 1

[[set]]
record = "ExampleItem.Drive_Alpha"
guid   = "00000000-0000-4000-8000-00000000a001"
field  = "Components[ExampleDriveParams].params.spoolUpTime"
value  = 3.5
```

A new record is cloned from one of the same struct and then named like any other:

```toml
[[record]]
struct = "ExampleItem"
name   = "ExampleItem.Drive_MyMod"
clone  = { record = "ExampleItem.Drive_Alpha" }
```

For recursive array cloning use
`clone = { record = "ExampleItem.Drive_Alpha", mode = "deep" }`; `clone_mode`
is not a pack key. Atomic application remains the default.
Deep mode copies array storage through inline structs and class-array elements,
with at most 16 nested struct steps below the copied root. Strong/weak pointers
and record references keep their targets. Invalid ranges, opaque structs, count
overflow and excessive recursion refuse the whole clone; even best-effort mode
cannot commit a partially deep clone. Earlier accepted operations stay intact.
`policy = "best_effort"` explicitly permits partial application; inspect the
per-record receipts rather than treating attempted operations as committed work.

Without a `guid`, it gets one derived from your plugin id and the record name, the same at every launch; without a `file`, it lives at `libs/foundry/records/sco/<plugin id>/<name>.xml`.

The whole format (`[[set]]`, `[[instance]]`, `[[append]]`, `[[record]]`, value forms, `atomic`) is in sco-core's [DataCore packs reference](../../docs/datacore.md#pack-format). The rules that matter most:

- **All or nothing per file.** If any override in a file can't be applied (a record or field the game no longer has, a value of the wrong type), none of that file applies, and `mod.log` says why. `atomic = false` lets each override apply on its own, for lists of unrelated tweaks.
- **Plugin order decides.** When two packs set the same field, the one later in plugin order (folder name order) wins, and `mod.log` names both.
- **The patch is applied when the game loads its data**, at startup. Changing a pack takes effect at the next launch.

To find names and paths, extract `Game2.dcb` from `Data.p4k` and use sco-core's `sco-dcb` tool ([how](../../docs/datacore.md)): `sco-dcb show Game2.dcb <record>` prints every field as the path a pack writes. Before you ship, and again after each game patch:

```sh
sco-dcb lint my_pack                  # syntax and shape, no game file
sco-dcb check Game2.dcb my_pack       # every override resolved against the game's file; exit 0 = all apply
```

## Build and check

A pack needs no compiler. `sco_add_pack(<id> DIR <folder>)` in CMake copies it into `data/plugins/<id>/` on install, or copy the folder by hand.

```sh
out/bin/sco-plugin-check out/data/plugins/travel_pack
```

lists every file the host would index and fails if there are none. For `datacore\` files it only reports that they were indexed; `sco-dcb lint` and `sco-dcb check` check their content.
