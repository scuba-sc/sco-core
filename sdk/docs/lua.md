# Lua plugins

A Lua plugin is a folder with `plugin.ini` (`kind = lua`, `entry = main.lua`) and its scripts. sc-offline's bundled `sco-lua` runtime runs the entry script once at load, in a sandbox. The script registers commands and subscribes to events; those callbacks run on the game thread.

> **SDK 2.4.0** (release candidate 1, tag `sdk-v2.4.0-rc.1`), unchanged plugin ABI 1.1. This page describes the `plugins/lua/` runtime and its operation-specific limits, not release availability or game-service readiness. Its tests exercise [`examples/greeter`](../examples/greeter) and [`examples/notebook`](../examples/notebook).

## The sandbox

The script sees only:

- `sco`: the functions below, mirroring [`sco_api.h`](../../docs/api-v1.md),
- `string`, `table`, `math`, `utf8`,
- the safe base functions: `assert`, `error`, `ipairs`, `next`, `pairs`, `pcall`, `select`, `tonumber`, `tostring`, `type`, `xpcall`, `rawequal`, `rawget`, `rawlen`, `rawset`, `setmetatable`, `getmetatable`, and `print` (which logs at `info`).

There is no `io`, `os`, `package`, `require`, `debug`, `dofile`, `loadfile` or FFI, and precompiled (bytecode) chunks are refused. A script can't read or write files or start programs; `sco.store` is storage without file names.

`setmetatable` refuses a `__gc` field (finalizers would run outside the limits below).

Limits, per script:

- **Steps**: loading the script, and each event callback, command or `run_on_game_thread` task, may take 1 million steps (a step is about one VM instruction; long `string.rep`, `find`, `gsub`, `table.sort` and `table.concat` calls count per item, and `string.byte`, `string.char`, `table.pack`, `table.unpack`, long `find` needles and long `[...]` classes count by size). Past that the call fails and the script is disabled; `pcall` can't catch it.
- **Memory**: 64 MiB, and 256 MiB for every script together. An allocation past either fails with a `not enough memory` error. A `pcall` in the script can catch that error; when it reaches the callback, command, task or load, the script is disabled.
- **Tasks**: 16 `run_on_game_thread` calls waiting at once. Past that it returns `false, "too_many"`.
- **Subscriptions**: 64 functions per event name, 16 event names. Past that `sco.subscribe` returns `false, "too_many"`.
- **Errors**: an error raised in a callback is caught and logged; a command that raises fails with the error text as its reply. After 3 errors the script is disabled.

A disabled script stays listed but does nothing: its event callbacks stop, its commands answer `unavailable`, and `sco.subscribe`, `sco.register_command` and `sco.run_on_game_thread` return `false, "unavailable"` (a script disabled while it loads is refused). `mod.log` gets `[<id>] error: script disabled: <why>`.

## Reload and cleanup

Script-only plugins remain reloadable: Lua cannot publish native service tables,
so merely reading a host service does not make the script a session-lifetime native
provider. Reload runs on the game thread at a safe point, not inside an executing
callback. It releases the old owner before closing its Lua state and runs the new
entry script with a fresh owner/state.

Old subscriptions, commands, pending tasks, widget panels and hotkeys are withdrawn before the
new script runs. Storage rolls back an unfinished transaction; committed data
survives. Reload does not lift step, memory, task or subscription limits. A fresh
state can run again after a budget violation, but an overrun disables that state
in the same way as before reload.

The game adapter is optional and separate from the neutral runtime. Linking it
alone exposes nothing: the host must register it. Each new script state exposes
published service tables plus inventory's static category vocabulary, which needs
no provider. Inventory snapshots still require an available provider and the
`game.inventory.read` capability. Game queries stay read-only; DataCore retains its
separately documented patch API. Neither a reload nor a
missing adapter grants native access, game mutation methods, files or FFI. Reload
of a script is not reload of its native service providers.

`tests/test_lua.cpp` exercises the real loader's mid-session unload/load path with
pending callbacks/resources and repeated budget failures. The same target runs
with `SCO_GAME_SC=OFF`; game-contract cases additionally check adapter absence,
missing services and present read-only services across reload with the pack enabled.

## `sco` functions

| Function | Returns | Does |
|---|---|---|
| `sco.host_version()` | string | `"sc-offline 0.8.0"` |
| `sco.has(name)` | boolean | Whether a capability is available on this game build |
| `sco.status(message)` | | Shows `<id>: message` on the status line |
| `sco.log(level, message)` | | Writes `[<id>] message` to `mod.log`. `level` is `"info"`, `"warn"` or `"error"` |
| `sco.subscribe(event, fn)` | `true`, or `false, err` | Calls `fn(event, data)` on each `event`: `game.ready`, `tick` (`data` = milliseconds), `game.exit` |
| `sco.unsubscribe(event, fn)` | `true`, or `false, err` | Removes that subscription |
| `sco.run_on_game_thread(fn)` | `true`, or `false, err` | Runs `fn()` on the next tick. At most 16 can wait at once (`too_many`) |
| `sco.register_command(t)` | `true`, or `false, err` | Registers a command (below) |
| `sco.invoke(name, ...)` | `true, reply`, or `false, err[, reply]` | Runs any command, sc-offline's or a plugin's, now. Arguments must match the command's types exactly (`int` takes an integer, not `"2"` or `2.0`). When the command itself fails, the third value is its reply |
| `sco.list_commands()` | table | `{ {name, title, help, capability, args = { {name, type, help}, ... } }, ... }` |
| `sco.bind_hotkey(chord, command, ...)` | `true` or `false, why, message` | Binds a key chord (`"ctrl+alt+h"`) to a command with the extra values as its arguments: typed by the command's definition when it is registered, else by their Lua types. `message` names who holds a taken chord. Withdrawn when the script unloads. Needs the host service `sco.ui` (`unavailable` without it). Tabs and overlays from Lua come later |
| `sco.unbind_hotkey(chord)` | `true` or `false, why, message` | Removes the script's own binding |

`err` is the result name from the C API in lower case: `"bad_arg"`, `"unavailable"`, `"not_found"`, `"too_many"`, `"crashed"`.

## Renderer-neutral widgets (`sco.widgets` 1.0)

This section shows the base 1.0 route. Current source/development kits also expose
the guarded 1.1 subtree/capability methods, presentation fields and row/PAGE events,
plus 1.2 stable tags; see [the widget guide](widgets.md#lua) for version negotiation,
feature checks and the distinction from the unchanged published RC1 bindings.

`sco.widgets` is always present. `available()` is true only when both complete,
nonnull table methods exist; `set_panel(id, title, nodes[, fn])` and
`remove_panel(id)` independently check their method and return `true` or
`false, err`. An absent/short/null service or disabled script returns
`false, "unavailable"`; the table is queried on each call, not cached.

```lua
if sco.widgets.available() then
  assert(sco.widgets.set_panel("my_plugin.main", "Example", {
    {kind="column", id=1},
    {kind="label", id=2, parent=1, label="Plain text"},
    {kind="button", id=3, parent=1, label="Press"},
    {kind="toggle", id=4, parent=1, label="Enabled", value=false}
  }, function(panel, event)
    if event.kind == "button" then sco.log("info", panel .. " pressed") end
  end))
end
```

`nodes` is a dense, one-based array of flat **preorder** nodes (not nested children).
Node `id` is a nonzero uint32 integer, unique within the panel. Optional `parent`
is `0` or a preceding `row`/`column` ID whose subtree remains contiguous.
Fields are read raw, never via `__index`/`__len`; unknown fields, sparse arrays,
wrong Lua types, embedded NUL and malformed host data are `bad_arg`.

| `kind` | Specific fields |
|---|---|
| `label`, `button` | `label` (required nonempty for button) |
| `toggle` | nonempty `label`, boolean `value` (default false) |
| `slider` | nonempty `label`, finite numeric `value`, `minimum`, `maximum` |
| `combo`, `list` | nonempty `label`, dense string `items`, **zero-based** `index` (`-1` = none) |
| `text_input` | nonempty `label`, string `text` |
| `table` | row-major string `items`, integer `columns` (1..16) |
| `row`, `column` | layout grouping; no value/items/text |

All nodes accept boolean `disabled` (default false); disabled ancestors also
suppress interaction. Unused fields must remain absent/zero, as in the native
contract. Strings must be valid UTF-8. IDs/titles are at most 63 bytes, node
labels/text/items 255 bytes, 256 nodes/panel, 256 items/node, 64 panels and 4096
live nodes across owners. Over-limit node data/host capacity returns `too_many`;
malformed ID/title returns `bad_arg`. Transient conversion buffers are charged
against Lua memory, and bounded parsing loops against the instruction budget.

On success the host copies all declarative data. Mutating the original table
later changes nothing; submit a replacement or an explicit subtree update.
Failure preserves the old tree **and callback**. The registry retains `fn` until successful replacement,
removal or ordinary unload; callers need not keep their own reference.

`fn(panel_id, event)` receives a copied Lua table with `node_id` and lowercase
`kind`. Button has no payload; toggle adds boolean `value`, slider numeric
`value`, combo/list integer `index`, and text input string `text`. In the base
1.0 route, no events exist for labels/tables/layout. Callbacks run under the host
owner's callout
and the existing shared Lua sandbox budget; errors are caught/logged and an
overrun disables the script. They may invoke commands or replace/remove their
panel; queued old-generation events are discarded. A currently executing
callback remains rooted on the Lua stack during reentrant replacement/removal.
Ordinary unload withdraws panels/events before closing the Lua state.

See [widgets](widgets.md) and [the Lua example](../examples/widgets_lua).
The targeted real-loader fixture is `ctest -R '^test_lua_widgets$'`; headless
synthetic events do not certify production GPU rendering or gameplay.

## Nested panels (`sco.panel_builder`)

`sco.panel_builder.new()` creates an independent builder. The runtime installs
this trusted text module before the entry script; no `require`, filesystem
access or extra library is needed. It is available even without the widgets
service. Check `sco.widgets.available()` before submitting a panel.

```lua
local b = sco.panel_builder.new()
b:column(function(ui)
  ui:row(function(ui)
    ui:toggle{
      label="Enabled", value=false,
      on_change=function(panel, event)
        sco.log("info", panel .. ": " .. tostring(event.value))
      end
    }
    ui:slider{
      label="Speed", value=0.5, minimum=0, maximum=1,
      on_change=function(_, event) sco.log("info", tostring(event.value)) end
    }
  end)
  ui:button{
    label="Apply",
    on_click=function(panel, event)
      sco.log("info", panel .. ": button " .. event.node_id)
    end
  }
end)

local nodes, dispatch = b:build()
if sco.widgets.available() then
  assert(sco.widgets.set_panel("my_plugin.main", "Settings", nodes, dispatch))
end
```

| Builder method | Result |
|---|---|
| `b:column(fn[, options])`, `b:row(fn[, options])` | Adds a layout, calls `fn(b)` synchronously for its children, then restores the enclosing parent; returns the layout's node ID |
| `b:toggle{label, value, disabled, on_change}` | Adds a toggle; `value` defaults to false; returns its node ID |
| `b:slider{label, value, minimum, maximum, disabled, on_change}` | Adds a slider with required finite numeric range/value; returns its node ID |
| `b:button{label, disabled, on_click}` | Adds a button; returns its node ID |
| `b:build()` | Returns a fresh dense preorder `nodes` array and `dispatch(panel_id, event)` |

Layout options accept optional `label` and boolean `disabled`. Control labels
are required, nonempty UTF-8 strings, at most 255 bytes, without NUL. `disabled`
and callbacks are optional; callbacks must be functions. A slider must satisfy
`minimum <= value <= maximum`. Unknown options (including manual `id`, `parent`
and `kind`), wrong types, invalid strings, an empty build and more than 256 nodes
raise Lua errors before submission. Options are read raw, without table
metamethods. `build()` is refused inside an open layout body. If a layout body
raises, that entire subtree is discarded and the prior parent restored;
an enclosing `pcall` can catch ordinary construction errors.

IDs are assigned in call order, starting at 1 for each builder. Layout bodies
produce contiguous subtrees; root nodes have `parent=0`, and nested nodes name
their enclosing layout. Duplicate display labels do not share IDs. Each build
copies the node data and captures a separate callback map. Changing options or
adding more nodes cannot change a previous build; mutating one returned array
does not change the builder or another snapshot. Callbacks are kept out of the
node fields accepted by `sco.widgets`.

`on_click` and `on_change` both receive `(panel_id, event)`, exactly as the raw
widgets callback does. The dispatcher calls only the matching node ID/kind;
missing callbacks, unknown IDs, mismatched kinds, disabled controls and children
of disabled layouts return false. A handled event returns true. Callback errors
propagate to the existing runtime error guard. Widget submission retains the
dispatcher and its callbacks until successful replacement, removal or unload;
the builder itself can be dropped after submission. Build snapshots do not
update toggle/slider values automatically: create or submit a replacement tree
when application state changes.

Callbacks may replace/remove the panel or invoke commands under the existing
widget lifecycle rules. Construction, layout bodies and dispatched callbacks
use the same step and memory limits as other Lua code; no sandbox restrictions
are lifted. Host ownership, title validity, service availability and aggregate
panel/node quotas remain checked by `sco.widgets.set_panel`.

The implementation is embedded Lua text in
`plugins/lua/sco_panel_builder.h`, included in the source SDK profiles.
`test_lua_widgets` checks generated trees against the real host validator,
typed event dispatch, rollback, snapshot/callback lifetime, reentrant
replacement/removal and callback budget enforcement. These are headless
synthetic checks, not in-game verification.

## Commands

```lua
sco.register_command{
  name  = "greeter.greet",          -- "<id>.<action>", at most 63 bytes
  title = "Greet",                  -- menu label, at most 63 bytes
  help  = "Says hi",                -- optional, at most 255 bytes
  capability = nil,                 -- optional: the has() name the command needs
  args  = {                         -- optional, at most 16
    { name = "name",  type = "string", help = "Who to greet" },
    { name = "shout", type = "bool" },
  },
  fn = function(name, shout) return "Hi, " .. name end,
}
```

`type` is `"int"`, `"float"`, `"string"` or `"bool"`. The host checks the arguments against `args` before calling `fn`, which gets them as plain Lua values in order. `fn` returns the reply shown on the status line (a string, or nothing); `error(...)` makes the command fail.

## `sco.store`

The plugin's own storage, kept across launches: a key-value store and SQL on its own SQLite database, which no other plugin can open (the host service [`sco.storage`](../../docs/storage.md)). Keys are strings of 1-255 bytes, values are strings (any bytes) of at most 1 MiB. [`examples/notebook`](../examples/notebook) uses it.

```lua
local st = sco.store
st.put("spots.home", "Lorville")
local home = st.get("spots.home")            -- "Lorville"; nil when there is no such key
for _, key in ipairs(st.keys("spots.")) do print(key) end

assert(st.begin())                            -- many writes: one transaction, much faster
st.exec("CREATE TABLE IF NOT EXISTS visits(place TEXT, at INTEGER)")
st.exec("INSERT INTO visits VALUES (?, ?)", { "Lorville", 1 })
assert(st.commit())

for _, row in ipairs(st.sql("SELECT place, at FROM visits WHERE at > ?", { 0 })) do
  print(row.place, row.at)
end
st.sql("SELECT place FROM visits", nil, function(row) print(row.place) end)   -- row by row
```

| Call | Returns |
|---|---|
| `sco.store.available()` | Whether the host publishes storage |
| `sco.store.get(key)` | The value (a string), or `nil` when there is none |
| `sco.store.put(key, value)` | `true` |
| `sco.store.delete(key)` | `true` when it removed a value, `false` when there was none |
| `sco.store.keys([prefix [, after [, limit]]])` | A list of keys that start with `prefix` and sort after `after`, in byte order; at most `limit` (1-1000, default 1000). For the next page pass the last key as `after` |
| `sco.store.tables([limit])` | Parallel lists of own table/view names and kinds (1 table, 2 view), in byte order; excludes `sco_kv` and `sqlite_%`; limit 1-1000, default 1000 |
| `sco.store.columns(table[, limit])` | Parallel lists of column names, declared types and flags (1 NOTNULL, 2 primary key), in declaration order; same limits |
| `sco.store.begin()`, `commit()`, `rollback()` | `true`. One transaction at a time, over key-value and SQL alike; until `commit` nothing is kept, and an unload or crash rolls it back |
| `sco.store.exec(sql [, params])` | The number of rows an `INSERT`, `UPDATE` or `DELETE` changed (other statements: not meaningful) |
| `sco.store.sql(sql [, params])` | A list of rows, each `{ column = value, ... }`; at most 1000 rows, more is `too_many` (add a `LIMIT`) |
| `sco.store.sql(sql, params, fn)` | Calls `fn(row)` for each row until it returns `false`, then `true` |

Every call fails with `nil, err, message`: `err` is the result name (`"bad_arg"`: a refused or malformed statement, a key too long, a transaction already open; `"failed"`: the statement ran and failed; `"too_many"`: the storage quota is full, or too many rows; `"unavailable"`: the host has no storage), and `message` the host's text when it left one (`"no such table: visits"`). A host without storage (an older one, or a product without a data folder) still has `sco.store`; every call answers `nil, "unavailable"`.

- **SQL**: one statement per call, any SQLite statement on the plugin's own database except those [storage refuses](../../docs/storage.md#sql-what-a-plugin-may-run) (`ATTACH`, `VACUUM`, `BEGIN` / `COMMIT` in SQL: use `begin` / `commit`, most `PRAGMA`s, the `sco_*` tables' schema). The key-value store is the table `sco_kv(key, value)`.
- **Parameters**: a list bound to `?`, `?NNN` or `:name` in order, at most 32: integers, numbers, strings (bound as TEXT), booleans (1 or 0); set `n` to bind trailing `nil`s as NULL (`{ "a", nil, n = 2 }`). Column values come back as integers, numbers and strings (TEXT and BLOB); NULL columns are absent from the row.
- **Steps**: each storage call, and each row or key read, counts 100 steps against the [budget](#the-sandbox), on top of the callback's own code.
- **Cursors** are the runtime's: `sql` closes its cursor on every path, including an error in `fn` or the budget running out. Nothing in `sco.store` names a file or a path.
- A transaction is the plugin's until `commit` or `rollback`: a callback that fails between `begin` and `commit` leaves it open (the next `begin` answers `bad_arg`), so roll back on your error paths.

`lua-check.lua` has an in-memory `sco.store` (key-value and transactions; `exec` and `sql` answer `unavailable`, since a stock Lua has no SQLite).

## `sco.settings`

The values of the `[settings]` your `plugin.ini` declares ([format](plugin-ini.md#settings)), read-only: the player changes them in the game menu (the host service `sco.settings`).

```lua
local speed = sco.settings.get("speed")      -- an integer, 5 until the player changes it
local mode  = sco.settings.get("mode")       -- an enum's choice, as a string
local v, err, msg = sco.settings.get("nope") -- nil, "not_found", "this plugin declares no setting 'nope'"
```

| Call | Returns |
|---|---|
| `sco.settings.get(name)` | A boolean, integer, number or string (`bool`, `int`, `float`, `string` or `enum`), or `nil, err, message`: `"not_found"` (no such setting), `"bad_arg"` (a NUL in the name), `"unavailable"` (a host without the service) |

There is no setter. Read again when you need the value (it follows the player's changes), for example inside a command or a tick handler. A failed read can leave a `last_error` note for the C++ and C# wrappers; ignore it from Lua. `lua-check` doesn't provide `sco.settings` yet.

## `sco.datacore`

Present only when the product publishes the host service [`sco.datacore`](../../docs/datacore.md#the-scodatacore-service) (check `if sco.datacore then`); `lua-check.lua` has no stand-in for it. It queues DataCore overrides, the operations of a data pack's `.toml`, from a script. In sc-offline the game loads DataCore before scripts run, so a committed patch is saved and **applies from the next launch**; overrides that never change belong in a [data pack](data-packs.md#game-data-datacore).

```lua
local eos = "ExampleItem.Drive_Alpha"
local p = sco.datacore.begin()            -- or begin({ atomic = false }); nil, err on failure
p:set(eos, "Components[ExampleDriveParams].params.spoolUpTime", 3.5)
local fast = p:add_instance("ExampleDriveParams", eos, "Components[ExampleDriveParams]")
p:set_pointer("ExampleItem.Drive_Beta", "Components[ExampleDriveParams]", fast)
assert(p:commit())
for _, r in ipairs(p:report()) do print(r.op, r.state, r.reason) end
```

| Call | Returns |
|---|---|
| `sco.datacore.state()` | `"open"` (before the load) or `"loaded"` |
| `sco.datacore.begin([{ atomic = false }])` | A patch, or `nil, err` |
| `p:set(record, field, value)`, `p:append(record, field, value)` | `true`, or `false, err` |
| `p:add_instance(type [, clone_record [, clone_field]])` | An instance (use it as a value or with `set_pointer`), or `nil, err` |
| `p:set_pointer(record, field, instance)` | `true`, or `false, err` |
| `p:add_record(type, name, clone_record [, guid [, file_path]])` | A new record cloned from `clone_record` (same struct), returned as an instance (its root; later calls also name the record by `name`, and `{ ref = name }` points a reference field at it), or `nil, err`. No `guid`: a stable one derived from the plugin id and name. `nil, "unavailable"` on a host whose `sco.datacore` is 1.0 |
| `p:commit()`, `p:discard()` | `true`, or `false, err` |
| `p:report()` | A list of `{ state = "queued" \| "applied" \| "skipped" \| "refused", op = n, reason = "..." }`: one per operation (`op` 1, 2, ...), then the patch's (`op` 0) |

Values: integers, numbers, strings (also enum options), booleans, `nil` (a null pointer), an instance from `add_instance`, `{ guid = "..." }`, `{ enum = "Option" }`, `{ ref = "RecordName" }` (a reference field's target). A `record` is a record name or `"guid:..."`. Errors are the same strings as elsewhere (`"bad_arg"`, `"not_found"`, `"unavailable"`, ...). Every call counts against the step budget like other host calls, and the sandbox is unchanged: scripts never touch files; the host writes the saved patch.

## `sco.game.actors`

Present only when the Star Citizen game pack publishes [`game.actors`](../../docs/game-services.md) (check `if sco.game and sco.game.actors then`); `lua-check.lua` has no stand-in for it. Lua gets the game services **read-only**: queries, never a function that changes the game, so a script owns no NPCs.

| Call | Returns |
|---|---|
| `sco.game.actors.local_player()` | Your actor id and entity id (integers; the game's 64-bit ids, which can come back negative), or `nil, err, message` (`"not_found"` before you've spawned, `"unavailable"` when `game.actors.local_player` isn't ready) |
| `sco.game.actors.health(entity_id)` | The actor's health pool and its maximum (numbers; the maximum is 0, not read yet), or `nil, err, message` (`"not_found"` when it isn't streamed in or isn't an actor, `"unavailable"` when `game.actors.health` isn't ready or the host has only `game.actors` 1.0). `state` isn't in Lua: it isn't available yet |

Ids are session handles: use them on the same tick, never store them.

## `sco.game.vehicles`

Present only when the Star Citizen game pack publishes [`game.vehicles`](../../docs/api-v1.md#gamevehicles-10-game-pack) (check `if sco.game and sco.game.vehicles then`); `lua-check.lua` has no stand-in for it. Read-only: scripts can list seats but can't seat, eject or power on (those take `self` and change the game; use C, C++ or C#). Ids are the game's 64-bit entity ids, carried in Lua integers bit for bit: pass them back unchanged and never store one.

| Function | Returns |
|---|---|
| `sco.game.vehicles.player_ship()` | The id of the ship you're aboard |
| `sco.game.vehicles.seats(ship)` | A list of seats, each `{ index, seat_id, occupant_id, priority, name, usable, usable_known, occupied, pilot }` (`index` counts from 0, as the game pack's), and `true` when the ship has more than were listed (256 at most) |
| `sco.game.vehicles.seat_occupant(ship, index)` | The id of the actor in the seat, 0 when it's empty |

On failure each returns `nil, err, message` (`err` as for `sco.store`: `"unavailable"`, `"not_found"`, ...; `message` from the game pack's `last_error`).

## `sco.game.creative`

Present only when the Star Citizen game pack publishes [`game.creative`](../../docs/api-v1.md#gamecreative-10-game-pack) (check `if sco.game and sco.game.creative then`); `lua-check.lua` has no stand-in for it. Read-only: scripts can see which toggles are on but can't switch any (those take `self` and change the game; use C, C++ or C#).

| Function | Returns |
|---|---|
| `sco.game.creative.state()` | A table `{ god_mode, fly, infinite_ammo, infinite_ship_ammo }` of booleans: the toggles that are on, whoever switched them on |

On failure it returns `nil, err, message` (`err` as for `sco.store`: `"unavailable"`, ...; `message` from the game pack's `last_error`).

## `sco.game.gamerules`

Present only when the Star Citizen game pack publishes [`game.gamerules`](../../docs/game/gamerules.md) (check `if sco.game and sco.game.gamerules then`); `lua-check.lua` has no stand-in for it. Read-only: scripts see the server GameRules parameters the client received, and cannot change them. The capability `game.gamerules.read` is OFF by default, so on a stock product `read()` answers `nil, "unavailable"`.

| Function | Returns |
|---|---|
| `sco.game.gamerules.read()` | A table `{ valid, generation, enforce_group_qt, quantum_travel_allowed, quantum_boost_allowed, travel_target_threshold_angle, target_lock_angular_player, target_lock_angular_ai, target_lock_linear_player, target_lock_linear_ai, boost_group_distance_threshold }`: the first, third, fourth and fifth are booleans, `generation` an integer (an opaque counter, not a session id) and the rest numbers. `valid` is `false` until the client has received a parameter set; every other field is then zero |

On failure it returns `nil, err` (`err` as for `sco.store`: `"unavailable"`, ...). There is no message: the table has no `last_error`.

## `sco.game.inventory`

The game adapter exposes this static category vocabulary whenever it is registered, including
when no inventory provider is published. It uses the existing public 192-member table for
**4.10.197.2878** (`item_type_build`, `item_type_count = 192`). This source addition is absent from
published RC1; `lua-check.lua` has no inventory stand-in. Names do not imply backend readiness.

| Member | Returns |
|---|---|
| `item_types` | Canonical category keys such as `Char_Armor_Helmet = 22`, `FPS_WeaponSidearm = 95`, and `UNKNOWN = 4294967295` |
| `item_type(raw)` | Mapped enum integer and the exact raw uint64 value; outside 0..191 maps to `UNKNOWN` |
| `item_type_name(raw)` | Canonical category name or `"UNDEFINED"`; no uint32 narrowing |
| `snapshot()` | Copied `{ size, count, generation, has_details, ports, flags, total_seen }`, or `nil, err` |

Inventory uint64 values (`item_class_id`, `entity_id`, `generation`, and the raw helper result)
are nonnegative Lua integers through 2^63-1, and decimal strings above it. The category helpers
accept an integer or an unsigned decimal string through `"18446744073709551615"`; negative,
fractional and out-of-range values raise an argument error. Do not convert upper-half values to
floating point. Other game services retain their documented id representations.

`snapshot()` requires a live script owner, capability `game.inventory.read` (OFF by default),
and a published complete non-null snapshot member. It prefers 1.1 and falls back to the 3088-byte
1.0 prefix only for `"unavailable"` / `"not_found"`. A 1.0 result has `has_details = false` and
no `flags` or `total_seen` fields. Each `ports` entry has `{ size, valid, item_class_id, entity_id,
item_type, item_type_name }`; invalid ports map to `UNKNOWN` / `"UNDEFINED"` while retaining the
raw field. Snapshots run on the game thread and own their copied values; no game pointers cross.

```lua
local inv = sco.game.inventory
local snapshot, err = inv.snapshot()
if snapshot then
  for _, port in ipairs(snapshot.ports) do
    print(port.item_type_name, port.item_class_id)
  end
end
```

See [inventory](../../docs/game/inventory.md) for snapshot limits and build scope. This route
adds no mutation, per-class ids, or in-game validation.

## `sco.game.entities`

Present only when the Star Citizen game pack publishes [`game.entities`](../../docs/api-v1.md#gameentities-11-game-pack) (check `if sco.game and sco.game.entities then`); `lua-check.lua` has no stand-in for it. Read-only: a Lua plugin owns no entities, so there is no spawn, despawn, move or watch.

| Function | Returns |
|---|---|
| `sco.game.entities.alive(id)` | `true` when the entity is streamed in |
| `sco.game.entities.class_of(id)` | The class name, or `nil, err, message` (`"not_found"` when the entity isn't streamed in, `"unavailable"` when `game.entities.class_of` isn't ready) |
| `sco.game.entities.get_transform(id)` | `x, y, z, qx, qy, qz, qw, zone_id` in the entity's zone's local frame, or `nil, err, message`. Ids are the game's 64-bit integers, which can come back negative |


## `sco.game.world`

Present only when the Star Citizen game pack publishes [`game.world`](../../docs/api-v1.md#gameworld-10-game-pack) (check `if sco.game and sco.game.world then`). Read-only queries (they take no `self`). Ids are the game's 64-bit ids in Lua integers bit for bit; positions are metres.

| Function | Returns |
|---|---|
| `sco.game.world.camera()` | `{ x, y, z, qx, qy, qz, qw, zone }`: the camera's position and rotation in the world frame, and the zone your player is in. No field of view (the game doesn't pin one) |
| `sco.game.world.raycast(zone, fx, fy, fz, dx, dy, dz, max_dist)` | `{ x, y, z, distance }` where a ray from `f` along `d` (any length, normalised) hit, in zone `zone`'s local frame (`max_dist` is clamped to 20,000 m). The hit entity and normal aren't available |
| `sco.game.world.free_camera_flag()` | `true` when the game's free-camera flag is set, else `false` (game.world 1.1). It only reads the flag; nothing pins what the flag shows on screen yet, so the capability `game.world.free_camera_flag` is off by default and the call answers `nil, "unavailable"` until the game pack turns it on, and on a host with only `game.world` 1.0 |

On failure each returns `nil, err, message` (`"not_found"` with the message "no hit within ..." when nothing was hit; `"bad_arg"`; `"unavailable"` when the capability `game.world.raycast` / `game.world.camera` / `game.world.free_camera_flag` isn't ready).

`game.hangar` and `game.reputation` have no Lua tables. Every function in them takes your plugin's handle (`self`), and the game adapter binds no game function that takes `self` other than a state query; the hangar's lift and ATC requests also change the game. Use C, C++ or C# for them.

## `game.*` events

When the Star Citizen game pack publishes them, `sco.subscribe` also takes the game's low-volume events; the handler's second argument is a table (nil for any other event, and for a payload shorter than this runtime knows):

| Event | Table |
|---|---|
| `"game.player.spawned"` | `{ entity_id, zone_id }` (`zone_id` is 0 if it couldn't be read yet) |
| `"game.player.died"` | `{ entity_id, killer_id }` (`killer_id` is always 0) |
| `"game.actors.died"` | `{ entity_id, killer_id }` for any actor (unreadable `entity_id` and `killer_id` are 0) |
| `"game.zone.changed"` | `{ old_zone_id, new_zone_id }` |

```lua
sco.subscribe("game.player.died", function(event, d)
  sco.log("info", "you died: entity " .. d.entity_id)
end)
```

Handlers run on the game thread, a frame or two after the game did it. An event whose capability (`game.events.player_spawned`, `.player_died`, `.zone_changed`) isn't ready never fires, so subscribing is harmless. `game.vehicle.boarded` / `exited` are reserved: not published yet. Ids are session handles: use them at once, never store them.

## Check a script

Without the game, with a stock Lua 5.4:

```sh
lua5.4 tools/lua-check.lua examples/greeter
lua5.4 tools/lua-check.lua examples/greeter --cap teleport --invoke greeter.greet "Pilot One" true
```

`lua-check.lua` runs the script with the sandbox's globals and a stand-in `sco` table that checks the rules above, fires `game.ready`, three ticks and `game.exit`, and runs every command without arguments plus the one named by `--invoke`. It has no instruction budget and no game.

## Widget extension limits

Lua supports bounded set/remove, guarded 1.1 subtree update/removal and
capabilities, tooltip/severity/paging fields, row/PAGE events and 1.2 stable tags
in current source/development kits. Each call checks the negotiated service,
complete member extent and required feature bits; unsupported requests refuse.
Lua exposes no native FFI. See [widgets](widgets.md) for operation-specific
support, callback lifetimes and the unchanged published RC1 boundary.
