// SPDX-License-Identifier: GPL-3.0-only
// Real host-kit admission and owner cleanup, with an entirely synthetic builtin.
#include "sco/app.h"
#include "sco/widgets.h"
#include "sco_widgets.h"
#include <cstdio>
static int passed = 0, failed = 0;
#define CHECK(c) do { if (c) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static const sco_widgets_v1* table = nullptr;
static sco_plugin* owner = nullptr;
static unsigned events = 0, unloads = 0;
static const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "widgetapp", "1.0", "scubamount"};
static const sco_plugin_info* Query() { return &info; }
static void Event(const char*, const sco_widget_event*, void*) { ++events; }
static sco_result Load(const sco_api* api, sco_plugin* self) {
    owner = self;
    const void* found = nullptr;
    const auto result = api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &found);
    CHECK(result == SCO_OK && found);
    if (result != SCO_OK || !found) return SCO_NOT_FOUND;
    table = static_cast<const sco_widgets_v1*>(found);
    sco_widget_node node{};
    node.size = sizeof(node); node.kind = SCO_WIDGET_BUTTON; node.id = 1; node.label = "Press";
    return table->set_panel(self, "widgetapp.main", "Panel", &node, 1, sizeof(node), Event, nullptr);
}
static void Unload() {
    ++unloads;
    CHECK(table && table->remove_panel(owner, "widgetapp.main") == SCO_UNAVAILABLE);
}
struct Headless final : sco::widgets::Renderer {
    void Render(const sco::widgets::Panel& panel, std::vector<sco::widgets::Event>& out) override {
        CHECK(panel.nodes.size() == 1);
        out.push_back({1, SCO_WIDGET_BUTTON, 0, 0, {}});
    }
};
int main() {
    const sco::plugins::Builtin plugin{"widgetapp", Query, Load, Unload};
    sco::app::Platform platform;
    platform.builtins = &plugin; platform.nBuiltins = 1;
    Headless renderer;
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(sco::app::Start(platform));
        CHECK(sco::widgets::Started());
        CHECK(sco::widgets::Panels().size() == 1);
        CHECK(sco::widgets::RenderPanel("widgetapp.main", renderer) == sco::Result::Ok);
        CHECK(events == i + 1);
        sco::app::Stop();
        CHECK(unloads == i + 1);
        CHECK(!sco::widgets::Started() && sco::widgets::Panels().empty());
        CHECK(sco::widgets::RenderPanel("widgetapp.main", renderer) == sco::Result::Unavailable);
        if (table) CHECK(table->remove_panel(owner, "widgetapp.main") == SCO_UNAVAILABLE);
    }
    std::printf("widgets app: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
