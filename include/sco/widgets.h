// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_widgets.h"
#include "sco/runtime.h"
#include <string>
#include <vector>

namespace sco::widgets {
// Owned, renderer-neutral snapshots. No plugin pointers escape in these views.
struct Node {
    uint32_t kind = 0, id = 0, parent = 0, flags = 0;
    int32_t index = 0;
    std::string label, text;
    double value = 0, minimum = 0, maximum = 0;
    std::vector<std::string> items;
    uint32_t columns = 0;
    // 1.1 node extension (zero/empty for 1.0-sized nodes).
    uint32_t severity = 0, first_row = 0, visible_rows = 0;
    std::string tooltip;
    bool extended = false; // submitted as sco_widget_node_1_1
    // 1.2 stable automation tag ("" = untagged; unique per panel; not rendered).
    std::string tag;
};
struct Panel {
    std::string id, title, owner;
    std::vector<Node> nodes;
};
struct Event {
    uint32_t node_id = 0, kind = 0;
    int32_t index = 0;
    double value = 0;
    std::string text{};
    int32_t row = -1; // SCO_WIDGET_EVENT_PAGE: requested first visible row
};
class Renderer {
public:
    virtual ~Renderer() = default;
    // Append candidate events only. Host validates them and dispatches after return.
    virtual void Render(const Panel& panel, std::vector<Event>& events) = 0;
};
// Start/Stop and table mutations/render are game-thread only. Stop off-thread is a no-op.
Result Start();
void Stop();
bool Started();
const sco_widgets_v1* Table();
// Game-thread snapshots (empty off-thread or when stopped).
std::vector<Panel> Panels();
// NotFound for absent panel, Unavailable when stopped; invalid adapter events
// are ignored, only the first MAX_EVENTS candidates are considered. Adapter
// exceptions return Failed. Replacement/removal during callbacks drops the tail.
Result RenderPanel(const char* id, Renderer& renderer);
// Render snapshots built so far (any thread). RenderPanel rebuilds a panel's
// shared snapshot at most once per call, and only after the panel changed.
uint64_t SnapshotBuilds();
} // namespace sco::widgets
