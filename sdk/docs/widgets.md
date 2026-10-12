# Widgets without renderer dependencies

For new native plugin UI, use the host-owned `sco.widgets` 1.0 service. Include
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

## Language coverage and legacy UI

C, C++20 and Lua support both methods. **C# is unsupported/deferred**:
[#142](https://github.com/scubamount/sco-core/issues/142) tracks UTF-8 marshalling,
layout pins and rooted callback ownership, a follow-up to
[#139](https://github.com/scubamount/sco-core/issues/139).

## Lua

Use `sco.widgets.set_panel(id, title, nodes[, fn])` and
`sco.widgets.remove_panel(id)`. `sco.widgets.available()` checks both complete
nonnull methods; each operation separately guards its member extent and queries
the optional service afresh. No renderer, native FFI or raw draw callback is exposed.

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
