# sco.widgets.view 1.0: the host-side panel reader

Status: tested with synthetic panels and synthetic threads. Not run in game. See [game capability status](game/status.md).

`sco.widgets.view` lets a host menu that runs on its own thread read the panels plugins publish through [`sco.widgets`](design/widgets.md), and send the player's clicks back. It is a C11 table in `include/sco/widgets_view.h`. A menu written in any language can use it. The header comment is the normative text. This page adds the per-method table and the rules in plain words.

## What it is, and what it is not

- A **host-source seam**. The header lives in `include/sco/`, beside `include/sco/host_menu.h`. It is not a plugin service and `query_service` never returns it (a test pins `SCO_NOT_FOUND` for the name).
- **Outside the SDK catalogue.** `tools/sdk_catalogue.py` reads only top-level `include/*.h`, so this header creates no contract record, no row in `docs/contracts.md`, no service name and no status-registry row. That is the intent: the table is not a plugin contract.
- **Stability.** The table is outside the Stable and Experimental classes of published service tables. It is pinned by size and layout (`tests/abi_widgets_view.c`) and promises nothing to plugins. It only grows: new functions are appended, each append raises the minor version macro, and `size` says which functions exist.
- **No capability.** It adds no capability and changes no default, so there is no status-registry entry.
- It ships in the `core-source` profile (so in the host-source kit) and in no plugin kit. A plugin author cannot see it.

## How the host gets the table

The panel state stays in `src/widgets/widgets.cpp`, behind its one widget lock. `src/widgets/view.cpp` is the table. The private header `src/widgets/view_internal.h` is the only way between them.

Host glue in `src/` uses three calls from that private header: `ViewTable()` and `ViewClient()` (both static for the life of the process, never freed or reused) and `ViewReleaseMenuThread()` (a backstop the control thread calls after it has joined the menu thread). Host code reaches them only through two functions in `sco/host_menu.h`, implemented in `src/app/sco_app.cpp`:

- `HostWidgetsView(context, &table, &client)`: game thread. It returns true, and sets both pointers, only while `HostMenuActive(context)` is true for the reserved host-menu identity. For any other caller, after stop, or with a null output it returns false and leaves both outputs null. The pointers it gives stay valid for the life of the process.
- `HostWidgetsViewReleaseMenuThread(context)`: control thread. It matches the context against the host-menu identities this process created (not the game-thread-only current instance). It clears a menu-thread registration the thread left behind, and returns false when nothing was registered or the context is not a host-menu identity. It does not check whether the menu thread still runs. Call it only after joining that thread.

`tests/test_host_menu.cpp` covers both with a synthetic thread. `include/sco/host_menu.h` includes `sco/widgets_view.h`.

## Methods

| Method | Threads | Results | Notes |
|---|---|---|---|
| `capabilities` | any | OK, BAD_ARG, UNAVAILABLE | prefix semantics like `sco.widgets` |
| `stamps` | any, atomics only, no allocation | OK, BAD_ARG, UNAVAILABLE | revisions, `event_drops`, `events_done` |
| `menu_thread_enter` | the future menu thread, not the game thread | OK, BAD_ARG, WRONG_THREAD, FAILED, UNAVAILABLE | Core records the calling thread |
| `menu_thread_leave` | the registered thread | OK, BAD_ARG, WRONG_THREAD | works after stop |
| `list_panels` | registered menu thread | OK, BAD_ARG, TOO_MANY (count = needed), UNAVAILABLE, WRONG_THREAD | registration order, released owners skipped |
| `copy_panel` | registered menu thread | OK, TOO_MANY (bytes = needed), NOT_FOUND, CRASHED, BAD_ARG, UNAVAILABLE, WRONG_THREAD | one consistent snapshot of one revision |
| `submit_event` | registered menu thread | OK, NOT_FOUND, BAD_ARG, TOO_MANY, UNAVAILABLE, WRONG_THREAD | queued, maybe merged |
| `dispatch_events` | game thread, not inside a widget callback | OK, WRONG_THREAD, FAILED, CRASHED, UNAVAILABLE | the only call that runs plugin code |

Order of checks in every call: NULL client (BAD_ARG), stopped (UNAVAILABLE), thread (WRONG_THREAD), arguments, panel. In 1.0 the read calls and `submit_event` are for the one registered menu thread only. "Any non-game thread" is not promised. Widening a thread rule later is compatible, narrowing it is not.

"Stopped" means the runtime is stopping or `sco.widgets` is not started. The same static handle works again after `sco::widgets::Start()` runs again. The counters, sequence numbers and the queue carry on across that restart.

## Layout pins

| Struct | Size | Key offsets |
|---|---|---|
| `sco_view_str` | 8 | offset 0, length 4 |
| `sco_view_panel_entry` | 224 | flags 4, revision 8, epoch 16, node_count 24, id 32, title 96, owner 160 |
| `sco_view_blob_header` | 96 | total_bytes 12, revision 16, epoch 24, node_count 32, nodes_offset 40, items_offset 44, strings_offset 52, panel_id 64, title 72, owner 80 |
| `sco_view_node` | 120 | flags 16, index 20, value 56, minimum 64, maximum 72, label 80, text 88, tooltip 96, tag 104, key 112 |
| `sco_view_event` | 56 | node_id 4, value 16, text 24, row 32, epoch 40, node_key 48 |
| `sco_widgets_view_v1` | 72 | version 4, capabilities 8, stamps 16, menu_thread_enter 24, menu_thread_leave 32, list_panels 40, copy_panel 48, submit_event 56, dispatch_events 64 |

`tests/abi_widgets_view.c` asserts all of these in C11, C++20 and (outside MSVC) with short enums, and `SCO_VIEW_ID_BYTES > SCO_WIDGETS_MAX_TAG`. `SCO_WIDGETS_CAP_MAX_TAG` (9) is an index into the `sco.widgets` capability array, never a byte length. The tag length limit is `SCO_WIDGETS_MAX_TAG` (63).

## The blob

One panel is one flat buffer: the header, the nodes (preorder, as the plugin submitted them), an item table of `(offset, length)` strings, and a string table. Everything is by offset. No pointer into plugin or Core memory leaves Core. A reader must still check every offset, length and UTF-8 sequence, and must refuse a blob whose three offsets are not multiples of 8 or whose ranges overlap.

Writer choices in 1.0 (a reader must not rely on them): items are laid out in node order, strings are not shared, the string table holds the node strings first and the three header strings last, and `total_bytes` is not padded. A password node keeps its flag and gets an empty text, so the blob never carries a secret. A Rust or C reader shows no dots for an existing value and only accepts new input.

`tests/test_widgets_view_reader.c` is a strict reader written in C against the header alone. The C++ test feeds it every blob it builds.

## Counters

All revisions and epochs come from one monotonic 64-bit source, so a value is never reused, even for a panel removed and added again. Zero is never a valid value.

| Counter | Moves when |
|---|---|
| panel `revision` | anything `copy_panel` would show changes: `set_panel`, `update_nodes`, `remove_nodes`, an event that changed a value, selection, text or page row. A BUTTON event changes nothing and does not move it |
| panel `epoch` | only `set_panel` (replace, or remove and add) |
| `global_revision` | any of the above, for any panel, and any list change |
| `list_revision` | a panel is added, removed or its owner released, or a listed panel's title, flags (tagged-event mode) or node count changes |
| `events_done` | a queue slot leaves the queue, after its effect is in the panel revision or after it was dropped |
| `event_drops` | a slot is dropped by `dispatch_events` |

The `revision` and `epoch` in a list entry are exact at the moment of the call. `list_revision` does not move for them, so a menu that wants the newest value of one panel uses the blob header.

## Matching events across a republish

A click is copied on the menu thread and dispatched on a later game-thread pass. A plugin can republish its panel in between. The click carries the epoch of the copy and the key of the node. The key is a 64-bit FNV-1a hash: for a tagged node, `'T'`, the kind and the tag; for an untagged node, `'U'`, the kind, the id, the parent and the label. The key function is written once in Core, and `tests/test_widgets_view.cpp` pins golden vectors. The C reader holds a second implementation and every blob node is checked against it.

At dispatch, against the live panel:

- Panel gone or owner released: dropped.
- Same epoch: the live node with the same id must have the same key, be enabled with its ancestors, and pass the usual `RenderPanel` checks.
- Different epoch: the live tagged node with that key is the target, otherwise the live node with the same id whose key matches. It receives the event with its own id.
- The callback tag is always the live node's tag. The menu never supplies one.

An untagged control that a plugin relabels between copy and dispatch loses that click. The drop is counted. The supported way to keep a control clickable across a relabel or a renumbering is a tag.

Two deliberate differences from `RenderPanel`, which is unchanged (its copy and dispatch happen in one pass, so neither gap exists for it):

1. `dispatch_events` compares the key at equal epoch. `RenderPanel` matches by id alone, which can press the wrong control after an id is reused.
2. `dispatch_events` has no generation break inside one drain. A callback that calls `update_nodes`, `remove_nodes` or `set_panel` does not drop the later slots. Each slot is matched on its own. A callback that removes its panel leaves nothing to match, so the rest of that panel's slots are dropped. The drain handles only the slots taken when it started, so it always ends.

`RenderPanel` and `dispatch_events` share one event function. `RenderPanel` passes "break on generation change" and no key.

## Sequence numbers, coalescing and `events_done`

`submit_event` gives each accepted event the next number: 1, 2, 3 and so on, with no gaps. A refused event takes no number. The queue is a list of slots, first in first out, each covering a range of numbers.

A SLIDER or TEXT_INPUT event merges into the newest slot if that slot is still queued and holds a SLIDER or TEXT_INPUT event for the same panel id, node id, kind, epoch and node key. The slot keeps its place and takes the new value. A merge needs no free slot, so it works when the queue is full (256 slots). Nothing else merges. A slot that `dispatch_events` has taken is never merged into.

`dispatch_events` takes the slots queued when it starts and handles them in order. It sets `events_done` to the end of each slot's range after the slot's effect is in the panel revision (or after it was dropped). So `events_done >= k` means event `k` is finished, and a copy taken after that shows its effect. `dispatched` and `event_drops` count slots, not events. A merged-away event counts in neither.

Example: BUTTON A (seq 1), SLIDER S 0.3 (2), SLIDER S 0.5 (3, merged). After one drain `dispatched` is 2 and `events_done` is 3. A poll sees 0, 1 or 3, never 2.

## Behaviour details

These are the simpler, safer readings. Change them only with a new minor version.

1. **Text.** A NULL `text` on a TEXT_INPUT event is the empty string. A non-NULL `text` on another kind is not refused at submit. It is dropped at dispatch, as `RenderPanel` drops it. `index`, `value` and `row` are not shape-checked at submit either. Only the cases the header lists answer BAD_ARG.
2. **Which kinds are events.** Only BUTTON, TOGGLE, SLIDER, COMBO, LIST, TEXT_INPUT and PAGE. A LABEL, TABLE, ROW or COLUMN kind is BAD_ARG.
3. **Released owner on submit.** `submit_event` answers OK for a panel whose owner was released after a fault (the panel still exists). The slot is dropped and counted at dispatch. `copy_panel` answers CRASHED for it and `list_panels` skips it.
4. **Noticing a fault.** There is no hook for "owner released". `list_revision` moves when Core handles the fault: `dispatch_events` returning CRASHED, or a `list_panels` or `copy_panel` call that finds the owner released. A menu that wants it sooner lists on a timer.
5. **Crashed slots.** After a faulting callback, the remaining slots of that owner find a released owner. Each is dropped and counted, and `dispatch_events` returns CRASHED. Other panels carry on.
6. **Stop during a drain.** If Core stops while `dispatch_events` runs, it returns UNAVAILABLE. The slot being handled and every slot after it have already left the queue, and no callback runs for them. Each is counted in `dropped` and `event_drops`, and `events_done` moves past them, so a menu waiting on `events_done` is not left waiting.
7. **Host panels.** A panel is flagged `SCO_VIEW_PANEL_HOST_OWNED` when its owner id is `menu`, the reserved host-menu id no plugin can claim.
8. **Snapshot build.** A dirty snapshot is built under the widget lock, possibly on the menu thread. Building it on the game thread at the end of each mutation is not done. It stays open if measurement in a real session shows contention.
9. **No render pass.** In a mode where Core does not render panels, a plugin gets no per-pass signal. Callbacks arrive only from `dispatch_events`, on the game thread. A plugin that refreshed state only on a render path must refresh on its own events or timer.

Removing a panel from inside its own callback drops the rest of that panel's queued slots. Replacing it does not ([matching](#matching-events-across-a-republish)).

## Tests

`tests/test_widgets_view.cpp`, `tests/test_widgets_view_reader.c` (a strict C reader written against the header alone) and `tests/abi_widgets_view.c` cover the rules above with synthetic panels and threads.

Not tested: a menu thread in the real game process, focus and input, and the real stop order of a product host. The accessors are tested only with a synthetic host-menu client and synthetic threads.

## Queue across Stop and Start

Each slot records the sco.widgets session it was queued in. A Stop or Start of sco.widgets moves the session. At dispatch, a slot from an older session is dropped and counted in `event_drops`, and `events_done` still advances past it. So an event queued before a restart is never delivered to a new panel that reuses the same id and tag.
