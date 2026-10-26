# Widgets without renderer dependencies

For new native plugin UI, use the host-owned `sco.widgets` service (1.0 with additive 1.1 and
1.2 extensions). Include
`include/sco_widgets.h` (C) or `include/scosdk/widgets.hpp` (C++20); no renderer
header, renderer library, GPU or game data is needed to build the example.
Frozen `sco_api` 1.1 discovers the optional service with `query_service`.

## Start with the example

[`examples/widgets`](../examples/widgets) contains a C11 plugin, manifest and
standalone CMake project. It submits LABEL, BUTTON, TOGGLE, SLIDER, COMBO, LIST,
TEXT_INPUT and TABLE inside ROW/COLUMN layout, using only made-up names/data.
Its button event invokes the registered `widgets.ping` command. Missing service
or a short/null table keeps the command working without UI.

From the unpacked core SDK:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build --config Release --prefix out
out/bin/sco-plugin-check out/data/plugins/widgets
```

The checker prints retained node kinds, six synthetic interactive events,
callback logs/command invocation and unload cleanup. It is a stand-in, not proof
of production game-thread scheduling, native fault containment or GPU rendering.
Windows game-loadable builds require an x64 Windows toolchain; macOS/Linux
binaries are development fixtures only.

## C++ wrapper

```cpp
#include "scosdk/widgets.hpp"

sco::sdk::Widgets widgets;
// api and self were received in sco_plugin_load; call on the game thread.
if (widgets.Open(api, self) == SCO_OK) {
    sco_widget_node label{};
    label.size = sizeof(label);
    label.id = 1;
    label.kind = SCO_WIDGET_LABEL;
    label.label = "Example status";
    (void)widgets.SetPanel("my_plugin.main", "Example", &label, 1);
    // Later, still on the game thread:
    (void)widgets.RemovePanel("my_plugin.main");
}
```

Interactive panels can use the C callback form of `SetPanel`, or a borrowed
object with `OnEvent(const char* panel, const sco_widget_event& event)`. The thunk
catches exceptions; keep the object alive until successful removal/replacement
or ordinary unload. `Open` returning OK does not promise every method exists:
each wrapper method separately checks its extent and function pointer.

### Declarative C++ builder

Include `scosdk/menu_builder.hpp` for `scosdk::PanelBuilder`. Nest `Row` / `Column`
bodies and bind controls to variables; `Build()` returns a validated, move-only
`PanelTree`. Chain attributes on the `WidgetRef` returned by a control:

```cpp
scosdk::PanelBuilder panel;
panel.Label("Example status").Tooltip("Synthetic help").Severity(SCO_WIDGET_SEVERITY_INFO);
int selected = 0; // keep the binding and the built tree alive until removal/replacement
panel.List("Examples", &selected, {"One", "Two", "Three"}).Paging(1, 2);
auto tree = panel.Build();
if (tree) (void)tree.Submit(widgets, "my_plugin.main", "Example");
```

Without extended attributes, `Build()` preserves the 80-byte 1.0 node layout.
Using `Tooltip(const char*)`, `Severity(uint32_t)` or `Paging(first_row, visible_rows)`
opts the entire tree into 104-byte `sco_widget_node_1_1` nodes, even for zero/NULL
attributes. Tooltips are copied, optional UTF-8 text up to 255 bytes; severity uses
`SCO_WIDGET_SEVERITY_*`. Paging is for LIST/TABLE only; zero visible rows disables
paging and requires first row zero, otherwise visible rows are 1..256 and first row
is zero or below the row count. Invalid attributes fail the build with `BAD_ARG`
or `TOO_MANY`, as the host would.

`Submit` checks that the host table covers the complete `capabilities` member and
that pointer is non-NULL before sending extended nodes. Otherwise it submits
80-byte 1.0 nodes, dropping tooltips, severity and paging while retaining base
values and callbacks. The built tree keeps its extended data for a later 1.1
submission. For direct `set_panel` calls use `tree.Nodes()`, `tree.Count()` and
`tree.Stride()` together, and perform the host version check yourself; do not index
an extended array as `sco_widget_node[]`. `Find(id)` accesses either layout.
Events update both bound values and the tree; PAGE events update the first visible
row without invoking the list's selection callback.

See [the full builder cookbook](../../docs/ui.md#cookbook-declarative-c-panels-scosdkmenu_builderhpp)
for validation and lifetime rules.

## Contract essentials

- `set_panel` submits the **whole** flat preorder tree; failure preserves the old
  tree and callback. Strings/items are copied on success; callbacks/context are not.
- Numeric node IDs are nonzero and unique within a panel; parent `0` is root,
  otherwise a preceding ROW/COLUMN with contiguous subtree. Labels never supply IDs.
- Panel IDs start with your plugin ID and a dot; 63-byte IDs/titles, 255-byte UTF-8
  label/input/item limits. Zero unused fields and NULL unused arrays.
- 64 panels, 256 nodes/panel, 4096 live nodes total, 256 items/node, 16 table columns.
- Methods and callbacks are game-thread only. Schedule worker updates through
  `run_on_game_thread`; manage task/context lifetime yourself.
- Callback events carry only the changed typed value, index or text (buttons
  have no payload). They may invoke a command or replace/remove the panel;
  generations discard the remaining stale events. Text/event pointers last only
  for the callback.
- `remove_panel` returns `NOT_FOUND` when absent. Check all results; bounds or
  allocation failure are `TOO_MANY`, malformed data `BAD_ARG`, stopped service
  `UNAVAILABLE`, off-thread mutation `WRONG_THREAD`.

Full tree/layout/lifecycle details and host integration live in
[the design contract](../../docs/design/widgets.md) and
[the per-method contract catalogue](../../docs/contracts.md). Host headers under `include/sco/`
and the optional ImGui adapter are source-kit internals, not public plugin headers.

## Stable tags (1.2)

A 1.2 node is `sco_widget_node_1_2` (128 bytes): the 104-byte 1.1 prefix,
a `const char* tag` at offset 104 and zero reserved words at offset 112. The host
copies the tag; it is not inline caller storage. The tag is
a stable automation identifier, `[A-Za-z0-9._-]{1..63}`, NULL/empty means
untagged. Tags are host metadata: they appear in panel snapshots (the C++
`Node::tag`) so renderers, bridges and automation can target nodes by a name
that survives node-id churn, but adapters never render them and event routing
stays numeric. Tags are unique per panel: a `set_panel`/`update_nodes` whose
resulting tree would carry the same non-empty tag twice is `BAD_ARG` and
leaves the panel untouched; a malformed tag (charset, over 63 bytes, nonzero
reserved) is `BAD_ARG`. A host reports support with
`SCO_WIDGETS_FEATURE_STABLE_TAGS`; `capabilities` answers the tag byte limit
at `SCO_WIDGETS_CAP_MAX_TAG` (array length 10 covers all 1.2 indices).
Plugins built for 1.0/1.1 are unaffected: submit the same 80/104-byte nodes
and the tag tail is invisible, exactly as 1.1 tails are to a 1.0 host.

## Language coverage and legacy UI

The typed C#/Lua 1.1/1.2 routes below describe current source/development kits.
The published `sdk-v2.4.0-rc.1` archive retains its earlier bindings; use the
documentation and wrappers from the same selected kit. Frozen plugin ABI 1.1
is unchanged.

C, C++20, C# and Lua support set/remove and the 1.1 update/remove subtree and
capability routes. C/C++ submit extended records with the matching stride; the
C++ wrapper checks complete tail member extents. Typed C# and Lua negotiate
1.2, then 1.1, then 1.0, falling back only for an absent/incompatible version.
Unsupported requested presentation features return an explicit refusal.
All existing provider admission, thread and owner lifetime rules still apply.

## C#

`Widgets.Open(plugin)` discovers the optional service; `SetPanel(id, title,
ReadOnlySpan<WidgetNode>, callback)` submits a complete bounded tree and
`RemovePanel(id)` removes it. Each method checks the complete function-pointer
extent and null pointer. `WidgetKind` covers all eight widgets plus Row/Column;
use `Disabled`, zero-based `Index`, row-major `Items`, and the native bounds.
Invalid UTF-16, embedded NULs, malformed trees and unused fields are rejected;
UTF-8 byte limits are checked before native allocation. Failed native mutations
preserve callbacks. Successful replacement/removal or ordinary unload releases
the owner registry root; copied `WidgetEvent` text is safe to keep. Callbacks may
invoke/replace/remove; exceptions stay managed and stale contexts are discarded.
Open again after owner reload. All calls and events remain game-thread only.

`WidgetNode` also accepts `Tooltip`, `Severity` (0..3), `FirstRow`, `VisibleRows`
and `Tag` under the public bounds above. Plain inputs retain the 80-byte route;
presentation inputs use 104-byte records and nonempty tags use 128-byte records.
The wrapper checks the negotiated version and advertised feature bits before
submission. `Capabilities(out WidgetCapabilities)` returns copied feature bits
and limits; incomplete tables and malformed capability counts are refused.

`UpdateNodes(id, nodes)` replaces one subtree in place or appends a new subtree
as the last child of its declared parent. `RemoveNodes(id, ids)` removes named
subtrees while retaining at least one node. These typed operations apply to
panels submitted by the same plugin through `Widgets`, including panels without
a callback. They keep copied validation state in the owner registry, update it
only after native success and preserve it on failure. A new wrapper for the same
loaded owner can use that state; reload requires a new panel submission.

`WidgetEvent.Row` is copied from the 40-byte event tail, with LIST row equal to
Index and other ordinary events set to -1. A paging event uses `WidgetKind.Page`
and its new first row in `Row`; Page is never a valid node kind. Truncated tails,
invalid fields, removed nodes and stale replacement contexts are dropped. The
host generation controls pending events after partial updates. Tags remain
metadata; numeric node IDs continue to route events.

The core archive includes `examples/cs_widgets` and its test driver:
`dotnet run -c Release --project csharp/Sco.Sdk.Tests -- --widgets`.
This builds/runs the example against a synthetic host, including missing service,
all typed events, reentrancy and unload. NativeAOT deployment uses
`dotnet publish examples/cs_widgets -c Release -r win-x64`; Windows native loading,
GPU rendering and gameplay are separate evidence, not claimed by the managed test.

## Lua

Use `sco.widgets.set_panel(id, title, nodes[, fn])` and
`sco.widgets.remove_panel(id)`. `sco.widgets.available()` checks both complete
nonnull methods; each operation separately guards its member extent and queries
the optional service afresh. No renderer, native FFI or raw draw callback is exposed.

`update_nodes(id, nodes)` and `remove_nodes(id, ids)` return the same success/error
shape as set/remove. They require the negotiated 1.1 service, complete member
extents and their advertised features; the provider validates and atomically
applies the resulting tree. `capabilities()` returns a fresh copied table or
`nil, error`: `count`, `features`, `version`, `max_panels`, `max_nodes`,
`max_total_nodes`, `max_items`, `max_columns`, `max_text`, `max_events`, and
`max_tag` when the provider wrote the 1.2 tail. No absent limit is invented.
Node fields `tooltip`, `severity` (0..3), `first_row`, `visible_rows` and `tag`
use the native bounds and feature admission. Unknown fields still refuse.
Extended events include `row` only after the complete 40-byte extent is present;
PAGE has `kind="page"` and its new first row in `row`. Stable tags never replace
numeric event IDs. Lua retains strings within its allocation budget and uses
opaque callback identities to reject callbacks from replaced panels or old
owner sessions.

```lua
if sco.widgets.available() then
  assert(sco.widgets.set_panel("my_plugin.main", "Example", {
    {kind="row", id=1},
    {kind="button", id=2, parent=1, label="Press"},
    {kind="text_input", id=3, parent=1, label="Name", text="Example"}
  }, function(panel, event)
    if event.kind == "text_input" then sco.log("info", event.text) end
    -- Legal here: invoke a command, submit a complete replacement, or:
    -- assert(sco.widgets.remove_panel(panel))
  end))
end
```

Nodes are dense one-based arrays of raw declarative tables, with lowercase `kind`,
nonzero integer `id`, optional `parent` and boolean `disabled`. All eight kinds
plus `row`/`column` are supported. Toggle values are boolean, slider values numeric,
combo/list indices **zero-based**, table cells row-major strings. The native
bounds/tree/UTF-8 rules apply; malformed fields return `false, "bad_arg"`, limits
`false, "too_many"`. Unknown fields and sparse arrays are rejected.

The host copies the tree; later table edits require resubmission. Registry roots
retain callbacks until successful replace/remove or ordinary unload. Failed calls
preserve the old tree/callback. Events are copied Lua tables containing `node_id`,
lowercase `kind`, and only the applicable `value`/`index`/`text` payload. Buttons
have no payload; toggle `value` is boolean. The owner's host callout guard and
shared Lua sandbox budget apply; callback errors are caught and logged. Reentrant
replace/remove invalidates queued stale generations without collecting a running
closure. Unload withdraws callbacks before the Lua state closes.

[`examples/widgets_lua`](../examples/widgets_lua) ships in core/core-source SDK
archives and installs with standalone CMake (no compilation required). The
`test_lua_widgets` real-loader harness loads it with and without a service, emits
six synthetic typed events and verifies unload. From a core-source extraction:

```sh
cmake -S . -B build -DSCO_GAME_SC=OFF
cmake --build build --target test_lua
ctest --test-dir build -R '^test_lua_widgets$' --output-on-failure
```

The generic `lua-check.lua` command stub is not renderer/service evidence; use the
real-host fixture above. See [the Lua field/event reference](lua.md#renderer-neutral-widgets-scowidgets-10).
Headless archive evidence is not GPU, Windows production scheduling or game validation.

## Legacy UI

Legacy `sco.ui` tabs/overlays, badges and hotkeys remain supported. Its draw
callback receives a host-specific opaque context (sc-offline: ImGui); that older
route still requires the host's matching third-party ABI and is not renderer-neutral.
New widget plugins do not include those ImGui headers or link a renderer.
