// SPDX-License-Identifier: GPL-3.0-only
// Private to src/widgets/ and to the host glue in src/. The panel state lives in widgets.cpp
// behind one lock; view.cpp (the sco.widgets.view table) reaches it only through these calls.
// None of them runs plugin code except DispatchEvent, which runs exactly one widget callback.
#pragma once
#include "sco/widgets.h"
#include "sco/widgets_view.h"
#include <memory>
#include <string>
#include <vector>

namespace sco::widgets::viewimpl {

// The key of a node (see sco/widgets_view.h). Never 0.
uint64_t NodeKey(const Node& node);
bool ValidUtf8(const std::string& s);

// True while sco.widgets is started and the runtime is not stopping. Atomics only.
bool Running();
// Any thread, atomics only. Both start at 0 and only grow.
uint64_t GlobalRevision();
uint64_t ListRevision();
// Moves on every Start and Stop of sco.widgets. Atomics only.
uint64_t Session();

struct ListRow {
    std::string id, title, owner;
    uint64_t revision = 0, epoch = 0;
    uint32_t nodeCount = 0, flags = 0; // SCO_VIEW_PANEL_*
};
// Widget lock only. Unavailable when stopped. Panels of a released owner are skipped.
Result List(std::vector<ListRow>& out);

struct Copied {
    std::shared_ptr<const Panel> panel;
    uint64_t revision = 0, epoch = 0;
    uint32_t flags = 0;
};
// Widget lock only; builds the shared snapshot if it is dirty. NotFound, Crashed (owner
// released), Unavailable.
Result Snapshot(const std::string& id, Copied& out);
// NotFound / Unavailable / Ok, for submit_event. Does not build anything.
Result Exists(const std::string& id);

enum class Step { Unavailable, Crashed, Gone, Dropped, Delivered, Failed };
// Game thread. Matches one view event by the rules at sco_view_event, validates it as
// RenderPanel does, applies it to the live node and calls the owner's callback.
Step DispatchEvent(const std::string& panelId, const Event& event, uint64_t epoch, uint64_t key);

// ---- host glue (not a plugin ABI) ----
// Static for the process lifetime, never freed. See sco/widgets_view.h.
const sco_widgets_view_v1* ViewTable();
sco_view_client* ViewClient();
// Clears a menu-thread registration the menu thread did not clear itself. The host calls it
// from the control thread after it has joined the menu thread. Returns true when it cleared one.
bool ViewReleaseMenuThread();

} // namespace sco::widgets::viewimpl
