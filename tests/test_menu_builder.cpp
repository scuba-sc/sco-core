// SPDX-License-Identifier: GPL-3.0-only
// scosdk/menu_builder.hpp: preorder tree shape, validation parity with the real sco.widgets 1.0
// host, and event dispatch back to bindings. Synthetic labels only; no renderer or game.
#include "scosdk/menu_builder.hpp"
#include "sco/widgets.h"
#include "sco/host.h"
#include "../src/api/internal.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace W = sco::widgets;
using sco::Result;
static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

struct ScriptedRenderer : W::Renderer {
    std::vector<W::Event> candidates;
    void Render(const W::Panel&, std::vector<W::Event>& events) override { events = candidates; }
};
static const sco_api* api = nullptr;
static const sco_widgets_v1* table = nullptr;
static sco_plugin* alpha = nullptr;
static const char* kPanel = "alpha.builder";

static sco_result HostSet(const sco_widget_node* nodes, uint32_t count, uint32_t stride = sizeof(sco_widget_node)) {
    return table->set_panel(alpha, kPanel, "Builder", nodes, count, stride, nullptr, nullptr);
}
static const W::Node* HostNode(uint32_t id) {
    static std::vector<W::Panel> panels;
    panels = W::Panels();
    for (const auto& panel : panels)
        if (panel.id == kPanel)
            for (const auto& node : panel.nodes)
                if (node.id == id) return &node;
    return nullptr;
}

static void TestShape() {
    bool god = false;
    float dist = 10.f;
    int spawned = 0;
    scosdk::PanelBuilder panel;
    panel.Column([&] {
        panel.Row([&] {
            panel.Toggle("God", &god);
            panel.Slider("Dist", &dist, 1.f, 50.f);
            panel.Button("Spawn", [&] { ++spawned; });
        });
        panel.Label("Status");
    });
    scosdk::PanelTree tree = panel.Build();
    CHECK(tree && tree.Result() == SCO_OK && tree.Error().empty());
    CHECK(tree.Count() == 6);
    const uint32_t kinds[] = {SCO_WIDGET_COLUMN, SCO_WIDGET_ROW, SCO_WIDGET_TOGGLE,
                              SCO_WIDGET_SLIDER, SCO_WIDGET_BUTTON, SCO_WIDGET_LABEL};
    const uint32_t parents[] = {0, 1, 2, 2, 2, 1};
    const sco_widget_node* n = tree.Nodes();
    for (uint32_t i = 0; i < 6 && i < tree.Count(); ++i) {
        CHECK(n[i].size == sizeof(sco_widget_node) && n[i].id == i + 1);
        CHECK(n[i].kind == kinds[i] && n[i].parent == parents[i]);
    }
    if (tree.Count() == 6) {
        CHECK(n[0].label == nullptr && n[1].label == nullptr);
        CHECK(std::strcmp(n[2].label, "God") == 0 && n[2].value == 0);
        CHECK(n[3].value == 10 && n[3].minimum == 1 && n[3].maximum == 50);
        CHECK(std::strcmp(n[4].label, "Spawn") == 0 && std::strcmp(n[5].label, "Status") == 0);
        CHECK(n[5].text == nullptr && n[5].items == nullptr && n[5].index == 0);
    }
    CHECK(scosdk::ValidateWidgetNodes(tree.Nodes(), tree.Count()) == SCO_OK);
    CHECK(HostSet(tree.Nodes(), tree.Count()) == SCO_OK);

    // Build() resets: ids restart at 1.
    panel.Label("Again");
    scosdk::PanelTree again = panel.Build();
    CHECK(again && again.Count() == 1 && again.Nodes()[0].id == 1 && again.Nodes()[0].parent == 0);

    // Siblings after a closed subtree attach to the right ancestor.
    panel.Row([&] {
        panel.Column([&] {
            panel.Label("a");
            panel.Row([&] { panel.Label("b"); });
        });
        panel.Label("c");
    });
    panel.Label("d");
    scosdk::PanelTree deep = panel.Build();
    const uint32_t deep_parents[] = {0, 1, 2, 2, 4, 1, 0};
    CHECK(deep && deep.Count() == 7);
    for (uint32_t i = 0; i < 7 && i < deep.Count(); ++i) CHECK(deep.Nodes()[i].parent == deep_parents[i]);
    CHECK(HostSet(deep.Nodes(), deep.Count()) == SCO_OK);

    // An exception from a body unwinds the nesting.
    bool threw = false;
    try {
        panel.Row([&] { throw 1; });
    } catch (int) {
        threw = true;
    }
    panel.Label("after");
    scosdk::PanelTree unwound = panel.Build();
    CHECK(threw && unwound && unwound.Count() == 2 && unwound.Nodes()[1].parent == 0);
}

static void TestEventsThroughHost() {
    bool flag = false, flag_seen = false;
    double amount = 2;
    int choice = -1, pick = 0, clicks = 0;
    std::string name = "Old";
    const std::vector<std::string> list_items{"One", "Two", "Three"};
    scosdk::PanelBuilder panel;
    panel.Column([&] {
        panel.Label("Synthetic");                                          // 2
        panel.Button("Press", [&] { ++clicks; });                         // 3
        panel.Toggle("Flag", &flag, [&](bool v) { flag_seen = v; });       // 4
        panel.Slider("Amount", &amount, 0.0, 5.0);                         // 5
        panel.Combo("Choice", &choice, {"Alpha", "Beta"});                 // 6
        panel.List("Items", &pick, list_items);                            // 7
        panel.TextInput("Name", &name).Password();                         // 8
        panel.Table("Values", 2, {"k", "v", "a", "1"});                    // 9
    });
    scosdk::PanelTree tree = panel.Build();
    CHECK(tree && tree.Count() == 9);
    if (!tree) {
        std::printf("build: %s\n", tree.Error().c_str());
        return;
    }
    CHECK(tree.Find(8)->flags == SCO_WIDGET_PASSWORD && std::strcmp(tree.Find(8)->text, "Old") == 0);
    CHECK(tree.Find(6)->index == -1 && tree.Find(6)->item_count == 2 && std::strcmp(tree.Find(6)->items[1], "Beta") == 0);
    CHECK(tree.Find(7)->item_count == 3 && tree.Find(9)->columns == 2 && tree.Find(9)->item_count == 4);

    sco::sdk::Widgets widgets;
    CHECK(widgets.Open(api, alpha) == SCO_OK);
    CHECK(tree.Submit(widgets, kPanel, "Builder") == SCO_OK);
    ScriptedRenderer r;
    r.candidates = {{3, SCO_WIDGET_BUTTON}, {4, SCO_WIDGET_TOGGLE, 0, 1}, {5, SCO_WIDGET_SLIDER, 0, 3.5},
                    {6, SCO_WIDGET_COMBO, 1}, {7, SCO_WIDGET_LIST, 2},
                    {8, SCO_WIDGET_TEXT_INPUT, 0, 0, "New \xE2\x9C\x93"}};
    CHECK(W::RenderPanel(kPanel, r) == Result::Ok);
    CHECK(clicks == 1 && flag && flag_seen && amount == 3.5 && choice == 1 && pick == 2);
    CHECK(name == "New \xE2\x9C\x93");
    // The tree follows the bindings; so does the host's retained copy.
    CHECK(tree.Find(4)->value == 1 && tree.Find(5)->value == 3.5 && tree.Find(6)->index == 1);
    CHECK(tree.Find(7)->index == 2 && name == tree.Find(8)->text);
    const W::Node* host_text = HostNode(8);
    CHECK(host_text && host_text->text == name);
    CHECK(tree.Submit(widgets, kPanel, "Builder") == SCO_OK);
    const W::Node* host_toggle = HostNode(4);
    CHECK(host_toggle && host_toggle->value == 1);

    // Moving the tree keeps the registered context.
    scosdk::PanelTree moved = std::move(tree);
    r.candidates = {{3, SCO_WIDGET_BUTTON}};
    CHECK(W::RenderPanel(kPanel, r) == Result::Ok && clicks == 2);
    CHECK(widgets.RemovePanel(kPanel) == SCO_OK);

    // A callback may remove the panel and destroy its own tree.
    std::optional<scosdk::PanelTree> holder;
    int destroyed = 0;
    panel.Button("Close", [&] {
        CHECK(widgets.RemovePanel(kPanel) == SCO_OK);
        holder.reset();
        ++destroyed;
    });
    holder.emplace(panel.Build());
    CHECK(*holder && holder->Submit(widgets, kPanel, "Builder") == SCO_OK);
    r.candidates = {{1, SCO_WIDGET_BUTTON}, {1, SCO_WIDGET_BUTTON}};
    CHECK(W::RenderPanel(kPanel, r) == Result::Ok);
    CHECK(destroyed == 1 && !holder.has_value() && HostNode(1) == nullptr);
}

static void TestDispatchRules() {
    bool on = false;
    float level = 1.f;
    int index = 0, clicks = 0, throws = 0;
    std::string text = "keep";
    scosdk::PanelBuilder panel;
    panel.Row([&] { panel.Button("Inside", [&] { ++clicks; }); }).Disabled();      // 1, 2
    panel.Button("Off", [&] { ++clicks; }).Disabled();                              // 3
    panel.Toggle("On", &on);                                                         // 4
    panel.Slider("Level", &level, 0.f, 2.f);                                         // 5
    panel.Combo("Pick", &index, {"x", "y"});                                         // 6
    panel.TextInput("Text", &text);                                                  // 7
    panel.Label("Plain");                                                            // 8
    panel.Button("Throw", [&] { ++throws; throw 7; });                               // 9
    scosdk::PanelTree tree = panel.Build();
    CHECK(tree && tree.Count() == 9);
    if (!tree) return;
    auto ev = [](uint32_t id, uint32_t kind, int32_t i = 0, double v = 0, const char* t = nullptr) {
        return sco_widget_event{sizeof(sco_widget_event), id, kind, i, v, t};
    };
    CHECK(!tree.Dispatch(ev(2, SCO_WIDGET_BUTTON)));                  // disabled row
    CHECK(!tree.Dispatch(ev(3, SCO_WIDGET_BUTTON)));                  // disabled button
    CHECK(!tree.Dispatch(ev(99, SCO_WIDGET_BUTTON)));                 // no such node
    CHECK(!tree.Dispatch(ev(0, SCO_WIDGET_BUTTON)));
    CHECK(!tree.Dispatch(ev(4, SCO_WIDGET_SLIDER, 0, 1)));            // kind mismatch
    CHECK(!tree.Dispatch(ev(4, SCO_WIDGET_TOGGLE, 0, 0.5)));
    CHECK(!tree.Dispatch(ev(4, SCO_WIDGET_TOGGLE, 1, 1)));            // stray index
    CHECK(!tree.Dispatch(ev(5, SCO_WIDGET_SLIDER, 0, 3)));            // out of range
    CHECK(!tree.Dispatch(ev(5, SCO_WIDGET_SLIDER, 0, std::numeric_limits<double>::quiet_NaN())));
    CHECK(!tree.Dispatch(ev(6, SCO_WIDGET_COMBO, 2)));
    CHECK(!tree.Dispatch(ev(6, SCO_WIDGET_COMBO, -2)));
    CHECK(!tree.Dispatch(ev(7, SCO_WIDGET_TEXT_INPUT, 0, 0, "\xff")));
    CHECK(!tree.Dispatch(ev(7, SCO_WIDGET_TEXT_INPUT, 0, 0, nullptr)));
    CHECK(!tree.Dispatch(ev(7, SCO_WIDGET_TEXT_INPUT, 0, 0, std::string(256, 'x').c_str())));
    CHECK(!tree.Dispatch(ev(8, SCO_WIDGET_LABEL)));
    CHECK(!tree.Dispatch(ev(1, SCO_WIDGET_ROW)));
    sco_widget_event short_event = ev(4, SCO_WIDGET_TOGGLE, 0, 1);
    short_event.size = sizeof(sco_widget_event) - 1;
    CHECK(!tree.Dispatch(short_event));
    CHECK(clicks == 0 && !on && level == 1.f && index == 0 && text == "keep");
    CHECK(tree.Dispatch(ev(5, SCO_WIDGET_SLIDER, 0, 2)) && level == 2.f);
    CHECK(tree.Dispatch(ev(6, SCO_WIDGET_COMBO, -1)) && index == -1);
    CHECK(tree.Dispatch(ev(9, SCO_WIDGET_BUTTON)) && throws == 1);   // exception swallowed
    scosdk::PanelTree::OnEvent("alpha.builder", nullptr, tree.Context());
    scosdk::PanelTree::OnEvent("alpha.builder", &short_event, nullptr);
    scosdk::PanelTree empty;
    CHECK(!empty && !empty.Dispatch(ev(1, SCO_WIDGET_BUTTON)) && empty.Count() == 0);
}

static void TestBuildErrors() {
    scosdk::PanelBuilder panel;
    auto expect = [&](sco_result want, const char* fragment) {
        scosdk::PanelTree tree = panel.Build();
        const bool ok = !tree && tree.Result() == want && tree.Nodes() == nullptr &&
                        tree.Error().find(fragment) != std::string::npos;
        if (!ok) std::printf("  got %d '%s', want %d '%s'\n", tree.Result(), tree.Error().c_str(), want, fragment);
        return ok;
    };
    CHECK(expect(SCO_BAD_ARG, "empty"));
    for (int i = 0; i < 257; ++i) panel.Label("x");
    CHECK(expect(SCO_TOO_MANY, "256 nodes"));
    float far = 60.f;
    panel.Slider("Far", &far, 1.f, 50.f);
    CHECK(expect(SCO_BAD_ARG, "node 1: slider"));
    float nan = std::numeric_limits<float>::quiet_NaN();
    panel.Slider("Nan", &nan, 0.f, 1.f);
    CHECK(expect(SCO_BAD_ARG, "finite"));
    panel.Label("ok");
    panel.Button("");
    CHECK(expect(SCO_BAD_ARG, "node 2: interactive"));
    panel.Label(std::string(255, 'x'));
    CHECK(panel.Build().Result() == SCO_OK);
    panel.Label(std::string(256, 'x'));
    CHECK(expect(SCO_TOO_MANY, "label longer"));
    panel.Label("\xC0\x80");
    CHECK(expect(SCO_BAD_ARG, "UTF-8"));
    panel.Label(std::string_view("a\0b", 3));
    CHECK(expect(SCO_BAD_ARG, "NUL"));
    panel.Toggle("Null", nullptr);
    CHECK(expect(SCO_BAD_ARG, "bound bool"));
    panel.Button("Masked").Password();
    CHECK(expect(SCO_BAD_ARG, "PASSWORD"));
    panel.Table("Odd", 2, {"a", "b", "c"});
    CHECK(expect(SCO_BAD_ARG, "whole number of rows"));
    panel.Table("None", 0, {"a"});
    CHECK(expect(SCO_BAD_ARG, "whole number of rows"));
    panel.Table("Wide", 17, {"a"});
    CHECK(expect(SCO_TOO_MANY, "16 table columns"));
    int index = 5;
    panel.Combo("Bad index", &index, {"a", "b"});
    CHECK(expect(SCO_BAD_ARG, "selection index"));
    std::vector<std::string> many(257, "i");
    index = 0;
    panel.List("Many", &index, many);
    CHECK(expect(SCO_TOO_MANY, "256 items"));
    // The first problem wins; later calls are still recorded harmlessly.
    panel.Button("");
    panel.Label(std::string(300, 'y'));
    CHECK(expect(SCO_BAD_ARG, "interactive"));
    // Build() inside a body fails without disturbing the outer build.
    sco_result inner = SCO_OK;
    panel.Column([&] {
        panel.Label("inside");
        inner = panel.Build().Result();
    });
    CHECK(inner == SCO_BAD_ARG);
    scosdk::PanelTree outer = panel.Build();
    CHECK(outer && outer.Count() == 2);
    // A failed tree never reaches the host.
    panel.Button("");
    scosdk::PanelTree bad = panel.Build();
    sco::sdk::Widgets widgets;
    CHECK(widgets.Open(api, alpha) == SCO_OK && bad.Submit(widgets, kPanel, "Builder") == SCO_BAD_ARG);
    // Reset drops recorded nodes and a pending error.
    panel.Button("");
    panel.Reset();
    panel.Label("fresh");
    CHECK(panel.Build().Count() == 1);
}

// ValidateWidgetNodes must answer exactly what the host's set_panel answers.
static void TestValidatorParity() {
    auto node = [](uint32_t kind, uint32_t id, uint32_t parent = 0) {
        sco_widget_node n{};
        n.size = sizeof(n); n.kind = kind; n.id = id; n.parent = parent; n.label = "Name";
        return n;
    };
    static const char* two[] = {"a", "b"};
    static const char* three[] = {"a", "b", "c"};
    static std::vector<const char*> many(257, "i");
    static const std::string long_label(256, 'x');
    struct Case { const char* what; std::vector<sco_widget_node> nodes; sco_result want; };
    std::vector<Case> cases;
    auto add = [&](const char* what, std::vector<sco_widget_node> nodes, sco_result want) {
        cases.push_back({what, std::move(nodes), want});
    };
    add("one button", {node(SCO_WIDGET_BUTTON, 1)}, SCO_OK);
    add("parent ahead", {node(SCO_WIDGET_ROW, 1), node(SCO_WIDGET_LABEL, 2, 5)}, SCO_BAD_ARG);
    add("parent is a label", {node(SCO_WIDGET_LABEL, 1), node(SCO_WIDGET_BUTTON, 2, 1)}, SCO_BAD_ARG);
    add("non-contiguous subtree", {node(SCO_WIDGET_ROW, 1), node(SCO_WIDGET_LABEL, 2, 1),
                                   node(SCO_WIDGET_LABEL, 3), node(SCO_WIDGET_LABEL, 4, 1)}, SCO_BAD_ARG);
    add("closed then sibling", {node(SCO_WIDGET_ROW, 1), node(SCO_WIDGET_COLUMN, 2, 1),
                                node(SCO_WIDGET_LABEL, 3, 2), node(SCO_WIDGET_LABEL, 4, 1)}, SCO_OK);
    add("duplicate id", {node(SCO_WIDGET_LABEL, 1), node(SCO_WIDGET_LABEL, 1)}, SCO_BAD_ARG);
    add("id 0", {node(SCO_WIDGET_LABEL, 0)}, SCO_BAD_ARG);
    add("kind 11", {node(11, 1)}, SCO_BAD_ARG);
    auto n = node(SCO_WIDGET_LABEL, 1); n.flags = 4; add("unknown flag", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_BUTTON, 1); n.value = 1; add("value on button", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_BUTTON, 1); n.index = -1; add("index -1 on button", {n}, SCO_OK);
    n = node(SCO_WIDGET_TOGGLE, 1); n.value = 0.5; add("toggle 0.5", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_SLIDER, 1); n.minimum = 2; n.maximum = 1; add("slider min>max", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_SLIDER, 1); n.maximum = std::numeric_limits<double>::infinity(); add("slider inf", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_COMBO, 1); n.items = two; n.item_count = 2; n.index = 2; add("combo index", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_COMBO, 1); n.items = two; add("items without count", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_LIST, 1); n.items = many.data(); n.item_count = 257; add("257 items", {n}, SCO_TOO_MANY);
    n = node(SCO_WIDGET_TABLE, 1); n.items = two; n.item_count = 2; n.columns = 17; add("17 columns", {n}, SCO_TOO_MANY);
    n = node(SCO_WIDGET_TABLE, 1); n.items = three; n.item_count = 3; n.columns = 2; add("ragged table", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_LABEL, 1); n.label = long_label.c_str(); add("256-byte label", {n}, SCO_TOO_MANY);
    n = node(SCO_WIDGET_LABEL, 1); n.label = "\xED\xA0\x80"; add("surrogate label", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_BUTTON, 1); n.label = nullptr; add("button without label", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_LABEL, 1); n.label = nullptr; add("label without text", {n}, SCO_OK);
    n = node(SCO_WIDGET_LABEL, 1); n.text = "x"; add("text on label", {n}, SCO_BAD_ARG);
    n = node(SCO_WIDGET_TEXT_INPUT, 1); n.flags = SCO_WIDGET_PASSWORD; n.text = "x"; add("password input", {n}, SCO_OK);
    n = node(SCO_WIDGET_LABEL, 1); n.size = sizeof(sco_widget_node) - 1; add("short node", {n}, SCO_BAD_ARG);
    std::vector<sco_widget_node> too_many;
    for (uint32_t i = 1; i <= 257; ++i) too_many.push_back(node(SCO_WIDGET_LABEL, i));
    add("257 nodes", too_many, SCO_TOO_MANY);
    for (const auto& c : cases) {
        const uint32_t count = static_cast<uint32_t>(c.nodes.size());
        std::string why;
        const sco_result mine = scosdk::ValidateWidgetNodes(c.nodes.data(), count, sizeof(sco_widget_node), &why);
        const sco_result host = HostSet(c.nodes.data(), count);
        CHECK(mine == host && mine == c.want);
        CHECK((mine == SCO_OK) == why.empty());
        if (mine != host || mine != c.want) std::printf("  case '%s': builder %d, host %d, want %d\n", c.what, mine, host, c.want);
    }
    std::string why;
    const auto dup = cases[5].nodes;
    CHECK(scosdk::ValidateWidgetNodes(dup.data(), 2, sizeof(sco_widget_node), &why) == SCO_BAD_ARG);
    CHECK(why == "node 2: duplicate id");
    const auto one = node(SCO_WIDGET_LABEL, 1);
    CHECK(scosdk::ValidateWidgetNodes(&one, 1, sizeof(sco_widget_node) - 8) == HostSet(&one, 1, sizeof(sco_widget_node) - 8));
    CHECK(scosdk::ValidateWidgetNodes(nullptr, 1) == SCO_BAD_ARG && scosdk::ValidateWidgetNodes(&one, 0) == SCO_BAD_ARG);
    // A wider stride with a larger node tail is accepted by both.
    struct Wide { sco_widget_node node; uint64_t tail; } wide[2]{{node(SCO_WIDGET_ROW, 1), 7}, {node(SCO_WIDGET_LABEL, 2, 1), 9}};
    wide[0].node.size = wide[1].node.size = sizeof(Wide);
    CHECK(scosdk::ValidateWidgetNodes(&wide[0].node, 2, sizeof(Wide)) == SCO_OK);
    CHECK(HostSet(&wide[0].node, 2, sizeof(Wide)) == SCO_OK);
    CHECK(table->remove_panel(alpha, kPanel) == SCO_OK);
}

int main() {
    sco::SetGameThread();
    api = sco::host::BuildApi({"test-menu-builder 1.0"});
    alpha = sco::host::NewPlugin("alpha");
    CHECK(W::Start() == Result::Ok);
    table = W::Table();
    TestShape();
    TestEventsThroughHost();
    TestDispatchRules();
    TestBuildErrors();
    TestValidatorParity();
    W::Stop();
    std::printf("menu builder: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
