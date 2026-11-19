# Renderer-neutral retained widgets

Status: C11, C++20, C# and Lua implementations with headless synthetic tests. Follow-up to
[SDK gap #139](https://github.com/scuba-sc/sco-core/issues/139). This is a new
host-owned `sco.widgets` service, currently **1.4**, with the 1.0 prefixes retained;
frozen `sco_api` 1.1 is unchanged.
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
commands. Version 1.1 adds atomic subtree updates/removal and bounded capabilities;
version 1.3 adds numeric slider presentation.

## Versioning

The service starts at independent major/minor version 1.0 and has a size-prefixed
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

How the adapter draws the declarative tree (layout rules live in `src/widgets/imgui_layout.h`):

- COMBO, SLIDER, TEXT_INPUT and TOGGLE with a label draw it on the left in one label column
  per container with the control on the same line, so a panel reads as an aligned form. The
  column is as wide as the widest sibling label, at most 40% of the container's content width
  and at least 100 px; a longer label is cut by an ellipsis with its full text in a tooltip.
  The control takes the whole rest of the row (in a two-column ROW, most of its column), and so
  does a control without a label. Only a container wider than 900 px caps its controls, at
  560 px. A COMBO shows its current value as the preview. The popup is capped at 640 px and
  about twelve rows, whatever the option names or count.
- LIST and COMBO options are one full-width selectable row each, with the text cut by an
  ellipsis to the row (full text in a tooltip). Selection events and paging are unchanged.
- A ROW made only of COLUMNs shares its width equally between them. A COLUMN or ROW with a
  label shows it as a section header. Pixel limits scale with the font size (16 px reference).
- An empty label draws nothing: an unlabelled ROW, COLUMN or TABLE starts with its content, with
  no blank line above it, so a row of buttons needs no made-up heading. A severity without a
  label shows the severity word alone. An empty LABEL node keeps its blank line (its label is
  its whole content).
- A NUMBER slider shows a whole value without decimals and any other with up to three, trailing
  zeros dropped: `30`, `0.5`, `0.125`, `1.235` for 1.23456. Dragging and keyboard steps still
  round the value to three decimals as before, a value typed with Ctrl+click is kept as typed, and
  clamping is unchanged, so events carry the same values as with the old `%.3f` text. Only the
  Ctrl+click edit box changed: it starts with the value printed with six decimals.
- Plugin text is never parsed as an ImGui label, with one exception: ImGui hides everything
  after `##` in a combo preview, so a preview containing it shows a space between the `#`s.

Widths are never taken from the available width of an auto-resizing window without a floor
(see the button note in the source), so such windows settle at a size instead of collapsing.

### Proposal: TABLE column headings (not implemented; needs the owner's decision)

A TABLE has no header row, so a seat or crew table shows columns nobody has named, and plugins
put the names in the first data row, where paging counts them as data. Proposed, as
the next versioned node tail (it changes the ABI, so nothing below exists until the owner
decides):

```c
#define SCO_WIDGETS_VERSION_1_5 0x00010005u          /* + TABLE column headings */
#define SCO_WIDGETS_FEATURE_TABLE_HEADINGS 0x400u
typedef struct sco_widget_node_1_5 {
    sco_widget_node_1_3 base;
    const char* const* headings; /* TABLE only: `columns` strings, or NULL for no header row */
    uint32_t heading_count;      /* 0, or exactly the node's columns; 0 for every other kind */
    uint32_t _reserved[3];       /* zero */
} sco_widget_node_1_5;
```

- Same rules as cells: plain UTF-8, at most 255 bytes each, deep-copied, never format strings
  or ImGui labels; an empty heading is a blank header cell. Any heading on a non-TABLE node, a
  count other than 0 or `columns`, or nonzero reserved words is `BAD_ARG`.
- Backward compatible the way 1.1 to 1.3 were: smaller nodes read as before; a 1.0 to 1.4 host
  ignores the tail and draws the table without headings, which is today's output. Plugins
  check version 1.5 and the feature bit through the complete `capabilities` member before
  relying on it; the C++ (`SetPanel15`/`UpdateNodes15`), C# and Lua wrappers refuse an older
  host rather than drop headings silently, as they do for `presentation`.
- Host: `Node::headings`; `update_nodes` replaces them with the node. No event changes: the
  header row is not a row index, so PAGE events, `first_row` and `visible_rows` count data rows
  only, and every page repeats the header row.
- Adapter: a header row drawn with `TableNextRow(ImGuiTableRowFlags_Headers)` and plain text in
  each cell (not `TableSetupColumn`/`TableHeadersRow`, which parse `##`); no sorting or resizing.
- Considered and not proposed: a `TABLE_FIRST_ROW_HEADINGS` flag reusing `items`. It needs no
  new pointer, but an older host refuses the unknown flag outright, and the header row would
  sit inside the paged row numbering.

Shipping it is an SDK minor release (new contract version, ABI tests, catalogue, C#/Lua
bindings and provenance in sco-core); sc-offline adopts it only after that SDK is published.

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

## Tagged events and old-provider refusal (1.4)

Version 1.2 adds stable ASCII node tags, `[A-Za-z0-9._-]{1..63}`, unique within
the panel. Version 1.4 adds `SCO_WIDGETS_FEATURE_EVENT_TAG` (`0x200`), distinct
from the existing `STABLE_TAGS` bit. Its `sco_widget_event_1_4` is 48 bytes:
the unchanged 40-byte event-1.1 prefix followed by `const char* tag` at offset 40.
The 32-byte base event and 48-byte service table retain their layouts.

A successful set/update containing a nonempty tag enables tagged events for
that panel. This mode remains enabled through subtree updates and removals,
even after the last tag disappears. A full `set_panel` replacement without
nonempty tags resets it. Every callback in tagged mode carries the full 1.4
event; an untagged target has a null tag. Tag-free legacy panels retain their
existing 32/40-byte events. Numeric node IDs still validate and route renderer
candidates; the callback tag is copied from the resolved canonical host node,
never supplied by the renderer. Tags are not rendered as labels.

Check `event->size >= sizeof(sco_widget_event_1_4)` before casting and then
read `((const sco_widget_event_1_4*)event)->tag`. The event, tag, text and panel
ID are borrowed only during the callback. Copy anything retained after return.
Replacement, updates, removal or ordinary unload during a callback cannot
invalidate that callback's copied tag; generation changes drop remaining stale
events. Malformed renderer candidates do not dispatch an event.

C callers requiring tagged events query `SCO_WIDGETS_VERSION_1_4`, guard every
table member used, and require both `STABLE_TAGS` and `EVENT_TAG` plus a usable
maximum tag length from capabilities. C++ offers `SetPanelTagged` and
`UpdateNodesTagged` for node-1.2 trees; those helpers negotiate/check support
before mutation. C# and Lua require the same 1.4 capability guarantee whenever
an input contains a nonempty tag. Their callbacks copy the tag into
`WidgetEvent.Tag` and `event.tag`, respectively. Untagged routes keep their
older-version fallback behavior. The managed bridge additionally checks that
the delivered tag matches the retained target; Lua validates the copied tag
and callback identity without maintaining a second full node tree.

An older provider cannot satisfy the new version/capability requirement, and
the guarded tagged routes return Unavailable before submitting a tree. Missing
services can return NotFound during discovery. Raw legacy `SetPanel` calls
remain unchecked for feature support: older providers may accept them while
ignoring unknown node tails. Such success is not a tagged-event guarantee.

The headless checker advertises 1.4 and exercises canonical copied tags,
reentrant mutation, sticky null tags, legacy-size reset and short-table refusal.
ABI, real-host, C++ fake-provider, managed and Lua fixtures cover these paths.
These are synthetic results. Not run in game. NativeAOT/archive-consumer and
product-host qualification remain separate from source fixture results.

## Host-side view

A host menu that runs on its own thread reads these panels, and sends the player's clicks back, through
`sco.widgets.view`, a host-source table that is not a plugin service. It is described in
[widgets-view.md](../widgets-view.md). Plugins see no change: the `sco.widgets` tables and `ImGuiRenderer`
behave as before.
