# UI: neutral widgets and legacy `sco.ui`

## Renderer-neutral widgets (`sco.widgets`)

For new native UI, use **`sco.widgets`**, the retained renderer-neutral service
in [`sco_widgets.h`](../include/sco_widgets.h) and [`scosdk/widgets.hpp`](../include/scosdk/widgets.hpp).
The host owns rendering; plugins submit copied preorder trees and receive typed
game-thread events. See [widget design](design/widgets.md) and
[SDK widget guide](../sdk/docs/widgets.md). The host publishes 1.1, a strictly additive
extension of 1.0 (next section). The C, C++20, C# and Lua routes in the SDK widget guide
cover 1.0, and each wrapper language has a declarative panel builder that writes the
preorder array for you. The C++ builder also opts into 1.1 attributes: see the cookbook below. C# is in
[the C# SDK guide](sdk-csharp.md) ("Panel builder"), Lua in
[the Lua guide](../sdk/docs/lua.md) ("Nested panels").

### `sco.widgets` 1.1

The host publishes **1.1** (`SCO_WIDGETS_VERSION_1_1`), a strictly additive extension: a
plugin that asks for 1.0 gets the same table and the same behaviour, and a plugin built
against the 1.0 header keeps its 80-byte node stride and 32-byte events. Contracts per
method: [contracts](contracts.md#scowidgets-sco_widgets_v1); pins: `tests/abi_widgets.c`.

| 1.1 addition | What it does |
|---|---|
| `update_nodes(self, panel, nodes, count, stride)` | Atomically replaces one subtree in place (same root id, same parent) or inserts a new one as the last child of its parent (`0` or a ROW/COLUMN). `nodes[0]` is the subtree root; the rest descend from it. Same node rules, limits and errors as `set_panel`; `NOT_FOUND` for no such own panel |
| `remove_nodes(self, panel, ids, count)` | Atomically removes each listed node with its subtree. `NOT_FOUND` for an absent id; refusing to empty the panel (use `remove_panel`) |
| `capabilities(self, caps)` | `caps[0]` = array length in, elements written out. Fills `SCO_WIDGETS_CAP_FEATURES` (`SCO_WIDGETS_FEATURE_*` bits) and the running host's limits (panels, nodes per panel, live nodes, items, columns, text bytes, events per pass). Ask instead of hard-coding the 1.0 macros |
| `sco_widget_node_1_1` | Opt-in node: `base.size = sizeof(sco_widget_node_1_1)` and a stride that covers it. Adds `tooltip` (plain UTF-8, <= 255 bytes), `severity` (`SCO_WIDGET_SEVERITY_INFO/WARNING/ERROR`) and, for LIST/TABLE, `first_row` + `visible_rows` paging (`visible_rows` 0 = no paging). A smaller node is read as 1.0 |
| `sco_widget_event_1_1` | Panels holding at least one 1.1 node get 40-byte events with `row` (PAGE: new first row; LIST: the selected row; else -1), and `SCO_WIDGET_EVENT_PAGE` when the user pages. Check `event->size >= sizeof(sco_widget_event_1_1)` before casting. Other panels keep 32-byte 1.0 events |

Check each member before calling it, as for any appended tail:

```c
const sco_widgets_v1* w = /* query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, ...) */;
if (w->size >= offsetof(sco_widgets_v1, update_nodes) + sizeof(w->update_nodes) && w->update_nodes) {
    sco_widget_node_1_1 status = {0};
    status.base.size = sizeof(status);
    status.base.kind = SCO_WIDGET_LABEL;
    status.base.id = 7;
    status.base.label = "Shields offline";
    status.severity = SCO_WIDGET_SEVERITY_WARNING;
    status.tooltip = "Synthetic example text";
    w->update_nodes(self, "example.main", &status.base, 1, sizeof(status));
}
```

On a 1.0 host the same 1.1 node array still works with `set_panel` (the tail is ignored),
but the table has no 1.1 members and events stay 1.0-shaped.

**Coalescing.** The host renders from a shared snapshot of each panel and rebuilds it at most
once per `RenderPanel` pass, and only when the panel changed since the last pass. Any
number of `update_nodes` / `remove_nodes` calls between frames cost one rebuild; an unchanged
panel costs none (`sco::widgets::SnapshotBuilds()` counts them for host tests). Structural
changes start a new generation, so like `set_panel` they drop the rest of the current pass's
events; the new tree renders next pass.

**Host adapter.** The optional ImGui adapter spells severity out as text (`Warning:`) as well as
coloring it, shows tooltips on hover, and draws only the visible rows of a paged LIST/TABLE
with Previous/Next controls that produce `SCO_WIDGET_EVENT_PAGE`. The scosdk C++ builder
exposes the node attributes; the wrapper's subtree/capabilities methods and the C#
and Lua 1.1 routes are not exposed yet. C and C++ call those methods directly. Nothing here has
run in the game.

### Cookbook: declarative C++ panels (`scosdk/menu_builder.hpp`)

[`scosdk/menu_builder.hpp`](../include/scosdk/menu_builder.hpp) (C++20, header-only, include it
explicitly) writes the flat preorder node array for you (1.0 by default, 1.1 with extended attributes). Nest `Row` / `Column`
bodies; every other call adds one widget to the innermost open one and binds it to your variable.

```cpp
#include "scosdk/menu_builder.hpp"

static bool              g_god = false;
static float             g_dist = 10.f;
static sco::sdk::Widgets g_widgets;
static scosdk::PanelTree g_tree;              // lives as long as the registered panel

void ShowPanel(const sco::sdk::Plugin& plugin) {  // game thread
    if (g_widgets.Open(plugin) != SCO_OK) return;  // optional service: the plugin works without UI
    scosdk::PanelBuilder panel;
    panel.Column([&] {
        panel.Row([&] {
            panel.Toggle("God", &g_god);
            panel.Slider("Dist", &g_dist, 1.f, 50.f);
            panel.Button("Spawn", [] { /* invoke a command */ });
        });
        panel.Label("Example status");
    });
    scosdk::PanelTree next = panel.Build();
    if (!next) return;  // next.Result(): SCO_BAD_ARG / SCO_TOO_MANY; next.Error(): "node 4: ..."
    if (next.Submit(g_widgets, "my_plugin.main", "My plugin") == SCO_OK) g_tree = std::move(next);
}
```

That produces the array a hand-written `set_panel` call would pass, ids being the 1-based
preorder position:

| id | kind | parent | from |
|---|---|---|---|
| 1 | COLUMN | 0 | `Column` |
| 2 | ROW | 1 | `Row` |
| 3 | TOGGLE | 2 | `Toggle("God", &g_god)`, value 0/1 from `g_god` |
| 4 | SLIDER | 2 | `Slider("Dist", ...)`, value from `g_dist`, range 1..50 |
| 5 | BUTTON | 2 | `Button("Spawn", fn)` |
| 6 | LABEL | 1 | `Label("Example status")` |

| Call | Node | On an event |
|---|---|---|
| `Label(text)` | LABEL | none |
| `Button(label, fn)` | BUTTON | calls `fn()` |
| `Toggle(label, bool*, fn(bool))` | TOGGLE | writes the `bool`, then `fn` |
| `Slider(label, T*, min, max, fn(T))` | SLIDER, `T` = `float` or `double` | writes the value, then `fn` |
| `Combo` / `List(label, int*, items, fn(int))` | COMBO / LIST, index -1 = none | writes the index, then `fn` |
| `TextInput(label, std::string*, fn(std::string_view))` | TEXT_INPUT | writes the string, then `fn` |
| `Table(label, columns, cells)` | TABLE, row-major cells | none |
| `Row(body)` / `Column(body)` | ROW / COLUMN | none |

Each call returns a `WidgetRef`: chain `.Disabled()` (a disabled row or column disables its
subtree), `.Password()` on a text input (display masking only), or read `.Id()`. Callbacks and
`items` / `cells` lists are optional where shown; every binding pointer is required.
Chain `.Tooltip(const char*)` for copied optional UTF-8 help text,
`.Severity(uint32_t)` using `SCO_WIDGET_SEVERITY_*`, or
`.Paging(uint32_t first_row, uint32_t visible_rows)` on a LIST/TABLE. These apply
to the returned control, for example:

```cpp
panel.Label("Example status").Tooltip("Synthetic help").Severity(SCO_WIDGET_SEVERITY_WARNING);
panel.Table("Values", 2, {"Name", "Value", "Example", "1"}).Paging(1, 1);
```

What to rely on:

- **Validation.** `Build()` checks the finished array against every `sco_widgets.h` rule
  `set_panel` enforces (preorder, contiguous subtrees, unique nonzero ids, kinds, unused fields,
  toggle/slider/index values, 256 nodes, 256 items, 16 columns, 255-byte UTF-8 strings,
  nonempty interactive labels, and 1.1 tooltip/severity/paging rules) and fails with the host's own code: `SCO_BAD_ARG` for malformed
  data, `SCO_TOO_MANY` for limits. Embedded NULs, missing bindings and `Build()` inside a body
  are `SCO_BAD_ARG` too. The first problem is reported; nothing throws. Hand-written arrays can
  use `scosdk::ValidateWidgetNodes(nodes, count, stride, &why)`. The panel id, title, ownership
  and the 4096-node live quota stay the host's to check. `tests/test_menu_builder.cpp` holds
  the validator to the real host's answers.
- **Values.** Bound values are read when the call is made. An event writes the binding, updates
  the tree's own node (so `Submit` again re-sends what is shown) and then runs the callback.
  A valid PAGE event updates the first visible row without invoking a selection callback.
  Malformed, disabled or unknown-node events are ignored, matching the host's event rules. Ids
  are positions, so a rebuild with a different shape renumbers; match on bindings, not ids.
- **Lifetimes.** Keep the tree and every bound variable alive until the panel is replaced,
  removed or the plugin unloads. A `PanelTree` may be moved freely (the registered context stays
  put) and a callback may `RemovePanel` and destroy its own tree. Replace a panel by building a
  new tree and swapping it in only after its `Submit` returns `SCO_OK`, as above: a failed
  `set_panel` keeps the old callback, which still points at the old tree.
- **Threads and errors.** Build, submit and receive events on the game thread. An exception
  from a callback is caught and dropped; it never crosses the C ABI. `Build()` and the widget
  calls are `noexcept`; only your `Row` / `Column` bodies can throw, and the nesting unwinds.
- **Version.** No 1.1 attribute preserves the 80-byte 1.0 output. Any `Tooltip`,
  `Severity` or `Paging` call, including zero/NULL values, makes `Build()` emit
  104-byte `sco_widget_node_1_1` nodes for the entire tree. `Submit` checks
  `table->size >= offsetof(sco_widgets_v1, capabilities) + sizeof(table->capabilities)`
  and a non-NULL `capabilities` pointer. A short/1.0 table or null pointer receives
  80-byte nodes instead, dropping the extended attributes, not the base values
  or callbacks. The built tree retains its 1.1 attributes. Direct submissions
  must pair `Nodes()` and `Count()` with `Stride()` and check the host themselves;
  `Find(id)` works for both layouts, but indexing `Nodes()` as a base array only
  works for 1.0. Paging follows the LIST/TABLE row rules above.

The sections below document the **legacy, still-supported `sco.ui` route**. Its
opaque draw context is host-specific (sc-offline passes an ImGui context), so a
drawing plugin on that route remains coupled to the host renderer ABI. Adding
widgets does not change existing tabs/overlays, badges, hotkeys or their callbacks.

Legacy plugins register draw callbacks in the product's menu and bind keys to commands. Draw callbacks use the product's renderer contract; only the registry is renderer-neutral. A plugin registers a **tab** (a page in the menu) or an **overlay** (drawn over the game every frame) with a draw function, and the product calls that function during its own frame. A plugin binds a **hotkey**, a key chord, to a command, and the product runs the command through the command registry when the key is pressed. sco-core's legacy UI registry keeps ids, order, lifetime and the hotkey table. That registry draws nothing and has no ImGui or game dependency; sc-offline renders it with its ImGui menu. The separate retained-widget service has an optional host-only renderer adapter.

It is a **host-owned service** (the reserved id `sco`, [API v1 § Host-owned services](api-v1.md#host-owned-services)) named `sco.ui`, version 1.0, found with the ordinary `query_service` of sco_api 1.1. `sco_api.h` is unchanged; the table is declared in [`include/sco_ui.h`](../include/sco_ui.h) (plain C) and pinned by [`tests/abi_ui.c`](../tests/abi_ui.c). The host kit (`sco::app::Start`) always publishes it.

## Surfaces

**`sco.ui.surfaces`** 1.0 ([`sco_ui_surfaces.h`](../include/sco_ui_surfaces.h),
[`scosdk/ui_surfaces.hpp`](../include/scosdk/ui_surfaces.hpp)) says *where* one of the
plugin's `sco.widgets` panels is shown: as a 2D HUD rectangle, as a camera-facing 3D billboard or
as a world-anchored 3D quad. It is a renderer-neutral, host-owned service: the host validates and
stores each surface per plugin (withdrawn on unload or crash) and draws nothing; the product's
renderer reads the registry through the internal `sco/surfaces.h` (`sco::surfaces::Surfaces()`,
`Revision()`) and projects each surface itself. Contracts:
[contracts](contracts.md#scouisurfaces-sco_ui_surfaces_v1); pins: `tests/abi_ui_surfaces.c`;
host tests: `tests/test_surfaces.cpp`. Nothing here has run in a game: the host side is covered by
headless tests, and projection is the renderer's job.

```c
#include "sco_ui_surfaces.h"

static const sco_ui_surfaces_v1* surfaces;
static uint32_t screen_id;

/* in sco_plugin_load, after set_panel(self, "hello.main", ...) */
if (api->size >= offsetof(sco_api, query_service) + sizeof(api->query_service) &&
    api->query_service(SCO_UI_SURFACES_NAME, SCO_UI_SURFACES_VERSION_1_0, (const void**)&surfaces) == SCO_OK) {
    sco_surface_pose pose = { sizeof(sco_surface_pose) };
    pose.pos[0] = 12.0; pose.pos[1] = -3.0; pose.pos[2] = 1.5;   /* world metres */
    pose.normal[1] = -1.0;                                     /* the face looks along -y */
    pose.up[2] = 1.0;
    pose.width_m = 1.6; pose.height_m = 0.9;
    pose.max_distance_m = 50.0;
    pose.flags = SCO_SURFACE_OCCLUDE_RAYCAST;
    pose.panel_id = "hello.main";
    sco_surface_desc desc = { sizeof(sco_surface_desc), SCO_SURFACE_QUAD_3D, &pose };
    if (surfaces->register_surface(self, &desc, &screen_id) != SCO_OK) { /* last_error says why */ }
}
```

| Kind | `pos` | `normal` | `up` | `width_m`, `height_m` | `max_distance_m`, `flags` |
|---|---|---|---|---|---|
| `SCO_SURFACE_HUD_2D` | centre in viewport fractions, x and y in [0, 1] (origin top-left), z = 0 | zero | zero | viewport fractions in (0, 1] | 0, none |
| `SCO_SURFACE_BILLBOARD_3D` | centre, world metres | zero (faces the camera) | unit | metres, (0, 1000] | [0, 1e7] m (0 = no limit); `OCCLUDE_RAYCAST` optional |
| `SCO_SURFACE_QUAD_3D` | centre, world metres | unit, the visible side | unit, perpendicular to `normal` | metres, (0, 1000] | as billboard |

Every double must be finite, coordinates within 1e12 m, unit vectors within 1e-3 of length 1,
and a quad's `normal . up` within 1e-3 of 0. `panel_id` must name a panel under the plugin's own
id; it need not exist yet. 32 surfaces per plugin, 256 in all. `update_surface_pose` replaces the
pose (and panel) under the same kind; `unregister_surface` removes it. Any thread; no callbacks.

### Projection (the renderer's side)

Notation: world points are column vectors; `C` is the camera position; `V` the view matrix and
`P` the projection matrix of the frame, `M = P V` (4x4); the viewport is `W x H` pixels with the
origin top-left. A panel is rendered into its own texture (or directly) in panel coordinates
`(s, t)` in [0, 1] x [0, 1], origin top-left, like the HUD.

**HUD rectangle.** No projection: the panel's screen rectangle has centre
`(pos[0] W, pos[1] H)` and size `(width_m W, height_m H)` pixels; panel point `(s, t)` is at
`x = (pos[0] + (s - 1/2) width_m) W`, `y = (pos[1] + (t - 1/2) height_m) H`.

**Surface frame.** Each 3D surface needs an orthonormal frame at its centre `p = pos`: a facing
`n`, a right `r` and an up `u`.

- Quad: `n = normal`; `r = normalize(up x n)`; `u = n x r`. (`up` is already perpendicular to `n`
  within 1e-3; re-orthogonalising this way removes the residue.)
- Billboard: `n = normalize(C - p)` (toward the camera, recomputed every frame);
  `r = normalize(up x n)`; `u = n x r`. When `up` and `n` are nearly parallel
  (`|up x n| < 1e-6`, the camera straight above or below), use the camera's own up vector in
  place of `up`, so the panel stays upright on screen.

With `(x, y, z)` as right, up, toward-viewer, `r = u x n` and the frame is right-handed.

**Panel to world.** Panel point `(s, t)` lies at

```
X(s, t) = p + (s - 1/2) w r + (1/2 - t) h u          w = width_m, h = height_m
```

so the corners are `p -/+ (w/2) r +/- (h/2) u`: top-left `(0, 0)`, top-right `(1, 0)`,
bottom-left `(0, 1)`, bottom-right `(1, 1)`. A renderer that draws the panel texture on these four
corners with perspective-correct interpolation needs nothing more.

**Homography.** `X` is affine in `(s, t)`, so homogeneous world point `[X; 1] = A [s; t; 1]` with
the 4x3 matrix

```
A = [ w r    -h u    p - (w/2) r + (h/2) u ]
    [  0       0               1           ]
```

and clip coordinates are `M A [s; t; 1]`. Keep rows 1, 2 and 4 of the 4x3 product `M A` (clip x,
y, w) to get the 3x3 homography `G`; with `q = G [s; t; 1]`, the normalised device coordinates are
`(q1 / q3, q2 / q3)` and the pixel is

```
x = (q1 / q3 + 1) W / 2          y = (1 - q2 / q3) H / 2
```

Folding the viewport into it gives the panel-to-pixel homography
`Hs = [[W/2, 0, W/2], [0, -H/2, H/2], [0, 0, 1]] G`, so `[x; y; 1] ~ Hs [s; t; 1]`.
For hit testing, the inverse maps a cursor pixel back into the panel:
`[s'; t'; k] = inverse(Hs) [x; y; 1]`, `(s, t) = (s'/k, t'/k)`; the cursor is on the panel when
both are in [0, 1] (and the surface is visible, below). `Hs` is invertible whenever the surface is
not seen exactly edge-on.

**Visibility**, in order:

1. Clip: skip the surface when any corner has clip `w <= 0` (behind the camera) unless the
   renderer clips the quad against the near plane itself.
2. Distance: with `max_distance_m > 0`, skip it when `|p - C| > max_distance_m`.
3. Facing (quads only): the visible side faces along `n`; skip it when `n . (C - p) <= 0`.
   Billboards always face the camera.
4. Occlusion: with `SCO_SURFACE_OCCLUDE_RAYCAST`, cast a ray from `C` toward `p` against the
   world; skip the surface when a hit lies closer than `|p - C| - epsilon` (a few centimetres,
   so the surface's own mounting geometry does not hide it). Casting at the four corners as well
   and hiding only when all are blocked keeps partly-visible surfaces up; either is acceptable.

The panel's widget tree is the same retained `sco.widgets` tree the menu shows; how the renderer
maps it to pixels (fonts, scale per metre) is the product's choice. Events from a projected panel
are delivered exactly as from the menu, through the panel's `sco_widget_event_fn`.

## Contents

- [Using it from C](#using-it-from-c)
- [Using it from C++](#using-it-from-c-1)
- [The table](#the-table)
- [Ids and order](#ids-and-order)
- [Drawing](#drawing)
- [Hotkeys](#hotkeys)
- [Threads](#threads)
- [Unload, crash and shutdown](#unload-crash-and-shutdown)
- [Hosting it](#hosting-it)
- [Lua](#lua)
- [Limits](#limits)

## Using it from C

```c
#include <stddef.h>
#include "sco_ui.h"

static int clicks;

/* frame is the product's frame context (sc-offline: its ImGui context), valid only now. */
static void draw_hello(void* frame, void* ctx) {
    (void)frame; (void)ctx;
    /* draw with the product's UI library, through frame */
}

sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    const sco_ui_v1* ui = NULL;
    if (api->size >= offsetof(sco_api, query_service) + sizeof(api->query_service) &&
        api->query_service &&
        api->query_service(SCO_UI_NAME, SCO_UI_VERSION_1_0, (const void**)&ui) == SCO_OK) {
        if (ui && ui->size >= offsetof(sco_ui_v1, register_tab) + sizeof(ui->register_tab) && ui->register_tab)
            ui->register_tab(self, "hello.main", "Hello", 100, draw_hello, &clicks);
        if (ui && ui->size >= offsetof(sco_ui_v1, set_badge) + sizeof(ui->set_badge) && ui->set_badge)
            ui->set_badge(self, "hello.main", "new");
        sco_arg who = { SCO_ARG_STRING };
        who.v.s = "Pilot";
        if (ui && ui->size >= offsetof(sco_ui_v1, bind_hotkey) + sizeof(ui->bind_hotkey) &&
            ui->bind_hotkey && ui->bind_hotkey(self, "ctrl+alt+h", "hello.wave", &who, 1) != SCO_OK) {
            char why[128];
            uint32_t n = sizeof why;
            if (ui->size >= offsetof(sco_ui_v1, last_error) + sizeof(ui->last_error) &&
                ui->last_error && ui->last_error(self, why, &n) == SCO_OK && api->log) api->log(self, SCO_LOG_WARN, why);   /* "ctrl+alt+h is bound by ..." */
        }
    }
    return SCO_OK;   /* a host without UI: keep working without it */
}
```

A host service stays valid for as long as any plugin is loaded, so keeping `ui` for the plugin's life is fine. A host that doesn't publish it answers `SCO_NOT_FOUND`.

## Using it from C++

[`scosdk/ui.hpp`](../include/scosdk/ui.hpp) wraps the table ([C++ SDK](sdk-cpp.md#ui)):

```cpp
#include "scosdk/ui.hpp"

struct Panel { void Draw(void* frame) { /* ImGui through frame */ } };
static Panel g_panel;

sco::sdk::Ui ui;
if (ui.Open(*this) == SCO_OK) {
    ui.AddTab("hello.main", "Hello", 100, g_panel);            // g_panel.Draw(frame) each frame the tab shows
    ui.SetBadge("hello.main", "3");
    if (ui.BindHotkey("ctrl+alt+h", "hello.wave", { sco::sdk::MakeArg("Pilot") }) != SCO_OK)
        Warn("hotkey: %s", ui.LastError().c_str());
}
```

An exception thrown by `Draw` is caught in the SDK's thunk and dropped; it never crosses the C ABI.

## The table

| Function | Does |
|---|---|
| `register_tab(self, id, title, order, draw, ctx)` | Adds a tab. `title`: 1-63 bytes. `SCO_BAD_ARG`: a bad or taken id, an id outside the plugin, a bad title, `draw` NULL. `SCO_TOO_MANY`: 256 tabs live |
| `unregister_tab(self, id)` | Removes one of the plugin's tabs and its badge. `SCO_NOT_FOUND` otherwise |
| `set_badge(self, tab_id, text)` | The short text beside the tab title (at most 15 bytes); NULL or `""` clears it. `SCO_NOT_FOUND`: not one of the plugin's tabs |
| `register_overlay(self, id, draw, ctx)` | Adds an overlay; overlays draw in registration order. Refusals as for tabs; 256 live |
| `unregister_overlay(self, id)` | Removes one of the plugin's overlays |
| `bind_hotkey(self, chord, command, args, nargs)` | Binds a chord to a command with up to 16 arguments, copied (strings up to 255 bytes). `SCO_BAD_ARG`: a bad chord, command name or argument, or the chord taken (bound by anyone, or reserved by the product). `SCO_TOO_MANY`: 512 bindings live |
| `unbind_hotkey(self, chord)` | Removes the plugin's binding on chord (any accepted spelling). `SCO_NOT_FOUND` otherwise |
| `normalize_chord(chord, out, inout_size)` | The normalized chord, with the raw-handler size handshake. `SCO_BAD_ARG`: not a chord |
| `last_error(self, out, inout_size)` | The message of the plugin's last refused call (`""` if none), with the size handshake |

Every function that takes `self` answers `SCO_BAD_ARG` for a handle the host didn't make or one already released, and `SCO_UNAVAILABLE` once the host has stopped the service. Strings are copied during the call; only `draw` and `ctx` are borrowed.

## Ids and order

- Ids are `<plugin id>.<name>`: `[a-z0-9_.]`, at most 63 bytes, no leading, trailing or doubled `.`, like command names. A plugin can only use ids under its own id, so two plugins never collide, and the id says whose tab it is. Tabs and overlays have separate id spaces (`hello.main` can name both).
- Tabs are shown by ascending `order`, ties in registration order (`sco::ui::Tabs()` returns them sorted). The product places its own tabs around them; sc-offline's are its built-in features.
- Overlays draw in registration order.

## Drawing

- `draw(frame, ctx)` runs on the **game thread**, during the product's frame, when the product draws that tab (usually: the menu is open on it) or every frame for an overlay.
- `frame` is the product's frame context, passed through untouched: sc-offline passes its ImGui context. It is valid only during the call. sco-core never looks at it; what a plugin may do with it is the product's contract (sc-offline: ImGui calls inside the tab's area, with the ImGui version sc-offline ships).
- A draw runs as a **callout of its plugin**, under the same crash guard as its commands, events and tasks (`sco::plugins::ContainCallouts`). A fault disables that plugin. Ordinary non-retained crash cleanup releases all of its UI; an exposed provider becomes restart-required `faulted` and retains tabs, overlays, badges and hotkeys without release hooks. Its guarded draws stop, but snapshots still enumerate retained entries. Every other plugin keeps drawing. The crash reason names the tab or overlay: `crashed in hello.main (0xC0000005)`.
- Nothing is locked while a draw runs: it may register, unregister (itself included) and set badges. An overlay removed during a pass is not called; one added during a pass draws from the next one.
- **Freeing `ctx`** after `unregister_tab` / `unregister_overlay`: on the game thread, as soon as the call returns (also from inside the draw itself). From another thread the game thread may be inside `draw` right now: call `run_on_game_thread` after unregister returns and free `ctx` in that task, as for `unsubscribe`.

## Hotkeys

A hotkey is a binding from a key chord to a command, kept by sco-core and dispatched by the product. Binding to a command, not to a callback, means the key does exactly what the menu's button does: the command registry checks the arguments and the capability, the command runs under its own plugin's crash guard, and a reply comes back.

**Chords** are modifiers and one key joined by `+`, case-insensitive, with spaces allowed around `+`:

| | Accepted |
|---|---|
| Modifiers | `ctrl` (`control`), `alt`, `shift` |
| Keys | `a`-`z`, `0`-`9`, `f1`-`f24`, `num0`-`num9` (`numpad0`-`numpad9`), `escape` (`esc`), `enter` (`return`), `tab`, `space`, `backspace`, `insert` (`ins`), `delete` (`del`), `home`, `end`, `pageup` (`pgup`), `pagedown` (`pgdn`), `up`, `down`, `left`, `right` |
| Punctuation, by name or character | `minus` (`-`), `equals` (`=`), `comma` (`,`), `period` (`.`), `slash` (`/`), `backslash` (`\`), `semicolon` (`;`), `apostrophe` (`'`), `grave` (`` ` ``), `lbracket` (`[`), `rbracket` (`]`) |

The host normalizes a chord to lower case, the modifiers in the order `ctrl`, `alt`, `shift`, then the key's first name: `Alt + Ctrl + Esc` is `ctrl+alt+escape`, `numpad5` is `num5`, `ctrl+-` is `ctrl+minus`. Refused: an empty part (`ctrl+`, `ctrl++a`), a modifier twice, two keys, modifiers alone, an unknown name (`win+a`, `page up`, `f25`).

**Conflicts.** One chord has one binding. Binding a chord already bound is `SCO_BAD_ARG`, whoever holds it, and `last_error` names the holder: `ctrl+alt+4 is bound by 'tester' to tester.go`. Plugins bind free keys; the first to bind a chord keeps it until it unbinds or unloads. (In game, sc-offline's F6 build mode collided with a test plugin's key: that is what this table prevents.)

**Reserved chords.** The product declares the keys it handles itself (`sco::app::Platform::reservedChords`, or `sco::ui::ReserveChord`) before plugins load. Binding one is `SCO_BAD_ARG` with `f6 is reserved by the host`, and the product never dispatches it.

**Dispatch.** When the product sees a chord it doesn't handle itself, it calls `sco::ui::Dispatch(chord)`: sco-core invokes the bound command with the stored arguments, with the binding's plugin as the caller, as `invoke` would. A command that doesn't exist (yet) or arguments that don't match its definition are answered by the registry at that moment (`NOT_FOUND`, `BAD_ARG`), not at bind time, so a plugin can bind a key to a command another plugin registers later.

## Threads

- Registration, badges, hotkeys, `normalize_chord` and `last_error`: any thread. Calls are serialized by one lock; it is never held while plugin code runs.
- Draws: the game thread only (`DrawTab` answers `WrongThread` elsewhere), because crash containment quarantines a faulting plugin, and ordinary `Release` runs on the game thread. A product whose renderer runs on another thread draws plugin UI from the game thread's side of its frame.
- `Dispatch`: any thread. On the game thread the command runs now and the reply comes back; from another thread the invoke is queued for the next tick (`Ok`) and the reply is dropped.

## Unload, crash and shutdown

- On ordinary unload, ordinary failed-load cleanup or non-retained crash cleanup, the host releases the owner: the release hook removes tabs, badges, overlays, hotkeys and its last error; its chords become free and later calls naming its handle are `SCO_BAD_ARG`. Restart-required retained-provider faults skip these hooks and retain resources, though guarded callbacks stop. If a nested consumer received a provider table before its load failed, the instance becomes `refused` with backing state retained: no owner release, resource hooks, export clearing or module closure. This is distinct from the fault path. See [native entrypoint contracts](contracts.md#native-plugin-entrypoints).
- `sco::app::Stop` closes runtime admission before tearing down still-loaded plugins, then stops the service: `sco.ui` is withdrawn, all remaining entries and reservations are cleared, and the table answers `SCO_UNAVAILABLE`. Retained faulted instances are not entered during plugin teardown.

## Settings

A plugin's typed `[settings]` ([plugin.ini grammar](plugins.md#typed-settings-settings)) are drawn by the product, in the plugin's menu page; the plugin sees none of it and only reads them through [`sco.settings`](api-v1.md#scosettings-10). sco-core has no renderer, so it exposes the settings as data through [`sco/settings.h`](../include/sco/settings.h) (library `sco_settings`) and the product turns them into widgets, next to the `sco.ui` tab registry it already walks:

```cpp
// Inside the menu window, on the game thread, after the plugin's own tab content:
for (const sco::settings::Page& page : sco::settings::Pages()) {      // one per plugin with settings
    // page.tab is the id of the plugin's first sco.ui tab (by order), or "" when it has none:
    // draw the entries under that tab, or list the plugin under a "Settings" section.
    for (const sco::settings::Entry& e : page.entries) {
        const sco::plugins::Setting& d = e.decl;                       // name, label, help, type, range, choices
        switch (d.type) {
        case sco::plugins::SettingType::Bool: {
            bool v = e.value.b;
            if (ImGui::Checkbox(d.label.c_str(), &v)) sco::settings::SetBool(page.plugin.c_str(), d.name.c_str(), v);
            break;
        }
        case sco::plugins::SettingType::Int: {
            int v = static_cast<int>(e.value.i);
            if (ImGui::SliderInt(d.label.c_str(), &v, static_cast<int>(d.minI), static_cast<int>(d.maxI)))
                sco::settings::SetInt(page.plugin.c_str(), d.name.c_str(), v);
            break;
        }
        case sco::plugins::SettingType::Enum: {
            if (ImGui::BeginCombo(d.label.c_str(), e.value.s.c_str())) {
                for (const std::string& c : d.choices)
                    if (ImGui::Selectable(c.c_str(), c == e.value.s)) sco::settings::SetString(page.plugin.c_str(), d.name.c_str(), c.c_str());
                ImGui::EndCombo();
            }
            break;
        }
        /* Float (SliderFloat / InputDouble, bounds when d.hasMin / d.hasMax) and String (InputText) likewise */
        }
        if (!d.help.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.help.c_str());
    }
}
```

| Function | Does |
|---|---|
| `Start()` / `Stop()` / `Started()` / `Table()` | Publish and withdraw `sco.settings` 1.0 (`sco::app::Start` does, after storage; `Stop` forgets every declaration) |
| `Declare(self, decls)` | Declares a plugin's settings and reads their kept values (`sco::app::Start` calls it from the manifest before the plugin loads) |
| `Pages()`, `Entries(plugin)` | Snapshots, any thread: `Entry` is the declaration and the current value; `Page` adds the plugin's first tab id |
| `SetBool` / `SetInt` / `SetFloat` / `SetString(plugin, name, v)` | What a widget calls (game thread; `SetString` sets a string or an enum choice). Checks the declaration, stores the value, dispatches `settings.changed`. `Ok` also for the current value (silent). `BadArg`: another type, out of range, not a choice. `NotFound`, `WrongThread`, or storage's error (then nothing changed) |

Ordinary unload and non-retained crash cleanup remove the plugin from `Pages()`. Restart-required retained-provider faults skip the settings release hook, so retained declarations still appear in snapshots. The values are kept in the plugin's storage namespace ([plugin.ini](plugins.md#typed-settings-settings) says how a stale one is dropped).

## Hosting it

The product side is [`sco/ui.h`](../include/sco/ui.h) (library `sco_ui`, [API: sco/ui.h](api.md#scouih-the-scoui-service)). `sco::app::Start` calls `ui::Start` and reserves `Platform::reservedChords`; another host calls them itself. A sketch of sc-offline's menu:

```cpp
static const char* const kReserved[] = { "f6", "f7", "f8" };   // build mode, teleport save / load
pf.reservedChords = kReserved;
pf.nReservedChords = std::size(kReserved);
sco::app::Start(pf);

// Each frame, on the game thread, inside the menu window:
for (const sco::ui::TabInfo& t : sco::ui::Tabs()) {          // sorted by order
    std::string label = t.badge.empty() ? t.title : t.title + " (" + t.badge + ")";
    if (ImGui::BeginTabItem((label + "###" + t.id).c_str())) {
        sco::ui::DrawTab(t.id.c_str(), ImGui::GetCurrentContext());
        ImGui::EndTabItem();
    }
}
sco::ui::DrawOverlays(ImGui::GetCurrentContext());           // menu open or not

// A key the product doesn't handle itself (and ImGui isn't typing into):
std::string chord = "ctrl+alt+4";                             // built from the key event
std::string reply;
if (sco::ui::Dispatch(chord.c_str(), &reply) == sco::Result::Ok && !reply.empty()) sco::Status("%s", reply.c_str());
```

| Function | Does |
|---|---|
| `Start()` / `Stop()` / `Started()` / `Table()` | Publish and withdraw `sco.ui` 1.0 (with the release hook); `Stop` clears everything |
| `NormalizeChord(text, out)` | The chord grammar above, for the product's own key events |
| `ReserveChord(chord)` | Reserves a chord for the product. `BadArg`: not a chord, or a plugin holds it |
| `Tabs()`, `Overlays()`, `Badge(id)`, `Hotkeys()`, `ReservedChords()` | Snapshots, any thread: tabs by order, overlays by registration, hotkeys by chord |
| `DrawTab(id, frame)` | Runs the tab's draw as a callout of its plugin. `NotFound`, `WrongThread`, `Crashed` |
| `DrawOverlays(frame)` | Runs every overlay; returns how many completed |
| `settings::Pages()` | The plugins' typed settings, as data to draw: see [Settings](#settings) |
| `Dispatch(chord, reply)` | Invokes the bound command through the registry. `NotFound` for no binding or a reserved chord |

### Optional third-party ABI coupling (not the frozen C ABI)

ImGui is **optional**: the kernel UI registry and plain-C `sco.ui` table do not require it. A plugin can use commands, events, settings or hotkeys without linking ImGui; on a host without a rendering contract, it must skip draw registration and continue without graphical UI.

The draw context is the product's choice: sco-core passes `frame` through as `void*`. The frozen `sco_api.h` 1.1 C ABI and the pinned `sco.ui` table describe registration and callout layout only. They do **not** freeze the contents of an `ImGuiContext` or grant cross-version compatibility to the pointer. Using that pointer with ImGui is a separate **third-party ABI coupling**, specific to the targeted host build.

A drawing plugin must match the host's exact ImGui source revision, public headers, unmodified `imconfig.h`, ABI-affecting compiler definitions and compatible build settings, and bind the host's context and allocator only during the draw callback. A release label or `IMGUI_VERSION_NUM` alone is not enough (especially for WIP builds). Rebuild drawing plugins when that identity or configuration changes; arbitrary ImGui versions must never consume the raw context. No generic runtime ImGui-version negotiation is provided by the frozen C table.

sc-offline hosts Dear ImGui **1.92.8 WIP**. Its public headers ship in the SDK at [`sdk/third_party/imgui/`](../sdk/third_party/imgui/README.md) (`imconfig.h`, `imgui.h`, `LICENSE.txt`), byte for byte the product's, and sc-offline's CI fails if they drift. Compile against them unmodified, `imconfig.h` included (`IMGUI_DEFINE_MATH_OPERATORS` is not defined); `imgui_internal.h` is not part of the contract.

## Lua

Scripts bind hotkeys; tabs and overlays need draw callbacks from Lua, which wait for the Lua UI bindings (G018).

| Function | Returns | Notes |
|---|---|---|
| `sco.bind_hotkey(chord, command, ...)` | `true`, or `false, why, message` | The extra values are the command's arguments, typed by its definition when it is registered, otherwise by their Lua types (integer, float, string, boolean). `message` is the host's (`"f6 is reserved by the host"`) |
| `sco.unbind_hotkey(chord)` | `true`, or `false, why, message` | |

## Limits

| Limit | Value |
|---|---|
| Live tabs, overlays | 256 each, every plugin together |
| Live hotkeys | 512 |
| Reserved chords | 256 |
| Id | 63 bytes |
| Title | 63 bytes |
| Badge | 15 bytes |
| Chord | 31 bytes normalized (63 as given) |
| Hotkey arguments | 16, strings up to 255 bytes |

## Developer reload

There is no renderer change for it. A product that wants a "Reload" button next to a plugin's tab calls `sco::app::Reload(id, &why)` on the game thread (or invokes the host command `sco.reload <id>`, [API v1](api-v1.md#host-commands)) and redraws from the next `sco::app::Plugins()`: the plugin gets a new handle, so a tab, overlay, badge or hotkey it registered is released with the old one and registered again by the new instance. A hotkey a plugin bound to a command is released with it too. `Reload` refuses built-ins and answers `Failed` "busy" from inside the plugin's own callout ([Plugins: hot reload](plugins.md#hot-reload)).
