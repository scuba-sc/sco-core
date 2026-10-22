# Renderer-neutral retained widgets

Status: C/C++ and Lua implementation; C# route deferred. Follow-up to
[SDK gap #139](https://github.com/scuba-sc/sco-core/issues/139). This is a new
host-owned `sco.widgets` **1.0** service, not a change to frozen `sco_api` 1.1.
Plugins include `sco_widgets.h` or `scosdk/widgets.hpp`; neither includes renderer
headers. The legacy `sco.ui` draw-context route remains supported separately.

## Problem and model choice

The existing `sco.ui` draw callback shares an opaque product renderer context.
It couples plugin binaries to the host's renderer ABI and cannot describe UI to
headless tests or language runtimes. It stays as a legacy compatibility route.

Choose retained declarative **complete-panel snapshots**, not an immediate-mode
plugin builder called each frame: the host copies and validates once, never
enters plugin code merely to draw, and can render the same data on any backend.
Atomic replacement bounds ownership, makes failure rollback explicit, and
keeps the first version small. Hosts apply validated interaction state before
sending guarded events back to plugin code; callbacks may invoke registered
commands. Diff/patch updates and richer styling are future service tails.

## Versioning

The new service has independent major/minor version 1.0 and a size-prefixed
function table. Future compatible operations append at the table tail; existing
field order, widths, discriminants and meanings never change. Nodes/events are
size-prefixed too; node arrays carry a stride so larger tails can be ignored
without changing element addressing. Both sides check complete member extents.
Incompatible semantics require a new service major. Frozen `sco_api` 1.1 and
its golden are unchanged.

## Representation and limits

`set_panel` adds or atomically replaces one complete panel owned by `self`.
Its nodes form a flat **preorder** tree. Node IDs are nonzero `uint32_t`, unique
within the panel, and unrelated to label text. Parent `0` means a root; every
other parent must be a preceding ROW/COLUMN node whose subtree is still open.
Subtrees are contiguous: returning to an ancestor after its subtree closes is
invalid. A panel can contain multiple roots; there are no separate edge arrays.

Panel IDs are `<plugin-id>.<name>`, lowercase `[a-z0-9_.]`, no empty dotted
segments, at most 63 bytes. Titles are nonempty UTF-8, at most 63 bytes.
Every node's `size` covers the complete 80-byte v1 node and fits inside
`node_stride`. The stride covers that struct, is aligned to its alignment, and
can include future ignored tails. The array itself must be aligned and readable.
The 1.0 table is 24 bytes on x64 (48 with the 1.1 tail); the event is 32 bytes.
Pins: `tests/abi_widgets.c`. 1.1 (subtree updates, opt-in node/event extensions,
paging, capabilities, snapshot coalescing): [UI](../ui.md#scowidgets-11).

| Bound | Limit |
|---|---:|
| Live panels, service-wide | 64 |
| Nodes per panel | 256 |
| Live nodes, service-wide | 4096 |
| Items/cells per node | 256 |
| Table columns | 16 |
| Label, input text and each item, excluding NUL | 255 UTF-8 bytes |
| Renderer event candidates inspected per panel pass | 256 |

Strings are UTF-8 NUL-terminated; malformed encodings are refused, never cut to
fit. Labels and cells are plain text, not format strings or renderer markup:
`%n` and `##` do not become formatting/ID directives. Interactive widgets require
nonempty accessible labels. Zero unused fields (unused `index` may be `0` or
`-1`); pass NULL `items` for zero items. Unknown kinds/flags are refused.

| Kind | Input state | Event payload |
|---|---|---|
| LABEL | `label` | None |
| BUTTON | `label` | `node_id`, BUTTON; no payload |
| TOGGLE | `label`, `value` exactly 0 or 1 | `value` |
| SLIDER | `label`, finite `value`, `minimum`, `maximum`; value in closed range | `value` |
| COMBO / LIST | `label`, `items`, `item_count`, `index` -1 or valid item | `index` |
| TEXT_INPUT | `label`, optional initial `text` | UTF-8 `text`, borrowed during callback |
| TABLE | `items` row-major cells; nonzero `columns`; count divisible by columns | None |
| ROW / COLUMN | Ordered layout children | None |

`SCO_WIDGET_DISABLED` prevents interactions. The supplied host adapter also
propagates disabled layout state through its descendants. The host validates
candidate events before mutating retained state, then invokes the callback;
label, layout and table nodes never dispatch events.

## Transactions, affinity and lifetime

All service mutations and events run on the **game thread**. Workers schedule
mutations with `run_on_game_thread`; the borrowed task context needs its own
lifetime management. A failure preserves the previous complete tree and callback:
there is no partially installed panel. Strings and arrays are deep-copied on
success; callback function/context are borrowed until successful replacement,
removal or ordinary owner release. No host allocation is transferred to plugins.

`BAD_ARG`: invalid/released owner, ID/title, size/stride, tree, kind/flags or
payload. `TOO_MANY`: size/capacity limits or allocation failure (ID/title syntax
and length are `BAD_ARG`). `WRONG_THREAD`: off game thread. `UNAVAILABLE`: stopped
service/host. `remove_panel` returns `NOT_FOUND` for an absent owned panel, not
success. Service lookup returns `NOT_FOUND` if not published and `UNAVAILABLE`
for an incompatible requested version. Check both full member extent and NULL
function pointers before calling; C++ `Widgets` does this per method.

Callbacks run under the owner's existing callout guard, outside the registry
lock. They may invoke commands or replace/remove their own panel. Successful
replacement invalidates the generation of all remaining events from that pass;
the new tree renders on the next pass. Removal (including remove-and-re-add),
ordinary unload and non-retained crash cleanup similarly prevent stale delivery.
The callback can free its old context after successful removal/replacement,
including from inside that callback; it must not subsequently access that context.
Event and panel-ID pointers are borrowed only for the callback.

A callback fault stops further dispatch through the owner guard. Native exposed
provider faults may retain backing resources until restart instead of ordinary
release; retained snapshots are not permission to call a faulted plugin. This
service does not change that existing lifecycle contract or sandbox native code.

## Host integration

`include/sco/widgets.h` and `src/widgets/widgets.cpp` are host-neutral source-kit
components, not plugin-public headers. Start the service on the game thread and
stop it after plugin teardown (`sco::app` does both automatically). `Panels()` returns owned snapshots; the product
chooses windows/menu placement. `RenderPanel(id, Renderer&)` calls a host
renderer with a snapshot and a candidate-event vector, validates those events,
updates state and dispatches guarded callbacks after the renderer returns.
Adapters never expose plugin callbacks or renderer context through the SDK.
An adapter exception returns `Failed`; invalid events are ignored. A GPU is not
needed: synthetic renderers use the same host entrypoint in `tests/test_widgets.cpp`.

`include/sco/widgets_imgui.h` and `src/widgets/imgui_adapter.cpp` are an optional,
**host-only** adapter. The product supplies the matching ImGui implementation
and backend. Build `sco_widgets_imgui` with `-DSCO_WIDGETS_IMGUI=ON` and
`SCO_WIDGETS_IMGUI_INCLUDE_DIR` pointing at those host headers; the host links
its ImGui implementation. The default kernel never links ImGui. These files belong to the source kit only; the plugin SDK profile
never exports them as public headers. This adapter does not change the older
opaque-context contract of `sco.ui`, remove its hotkeys, or force old plugins to
migrate. Actual window placement, styling and GPU/game validation are product work.

## Language and evidence boundary

- C: direct `sco_widgets_v1.set_panel` / `.remove_panel`.
- C++20: `sco::sdk::Widgets`, including a borrowed object `OnEvent` thunk that
  catches C++ exceptions before they cross C ABI.
- C#: `Sco.Sdk.Widgets` with pinned layouts, bounded copied UTF-8 trees, typed
  events and rooted callbacks; `Sco.Sdk.Tests --widgets` covers the adapter.
  Windows NativeAOT loading remains separate from managed synthetic-host evidence.
- Lua: `sco.widgets.set_panel` / `.remove_panel`, bounded raw declarative tables,
  registry-rooted callbacks, shared sandbox budgets and ordinary unload cleanup.
  `test_lua_widgets` exercises the real neutral host plus explicit partial/null fixtures.

`sdk/contracts/sco_widgets_v1.json` registers both methods and each language
route with source tokens/tests. `sdk/examples/widgets` uses every widget kind,
synthetic data only and a button-to-command callback. `sco-plugin-check` provides
a copied-tree stand-in and deterministic headless interactions; it is not the
production thread scheduler or native crash guard. `sdk/test-zip.sh` checks the
example from extracted archives; `SCO_SDK_WIDGETS_ONLY=1` narrows that check to
the widget plugin and checker. No ImGui, GPU, game image or new dependency is
required for this native example.
