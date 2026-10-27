// SPDX-License-Identifier: GPL-3.0-only
// sco.widgets 1.1 against the real host service: a frozen 1.0 binary layout,
// subtree updates/removal, node/event extensions, paging, capabilities and
// render-snapshot coalescing. Headless: no renderer, GPU or game.
#include "sco/widgets.h"
#include "sco/host.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace W = sco::widgets;
using sco::Result;
static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

// Private copies of the 1.0 layouts, as a plugin built against the 1.0 header has them.
struct Node10 {
    uint32_t size, kind, id, parent, flags;
    int32_t index;
    const char* label;
    const char* text;
    double value, minimum, maximum;
    const char* const* items;
    uint32_t item_count, columns;
};
struct Event10 { uint32_t size, node_id, kind; int32_t index; double value; const char* text; };
using Event10Fn = void (*)(const char*, const Event10*, void*);
struct Table10 {
    uint32_t size, pad;
    sco_result (*set_panel)(sco_plugin*, const char*, const char*, const Node10*, uint32_t, uint32_t, Event10Fn, void*);
    sco_result (*remove_panel)(sco_plugin*, const char*);
};
static_assert(sizeof(Node10) == 80 && sizeof(Event10) == 32 && sizeof(Table10) == 24);

struct HeadlessRenderer : W::Renderer {
    std::vector<W::Panel> recorded;
    std::vector<W::Event> candidates;
    void Render(const W::Panel& panel, std::vector<W::Event>& events) override {
        recorded.push_back(panel);
        events = candidates;
    }
};
static const sco_widgets_v1* table = nullptr;
static sco_plugin* alpha = nullptr;
static sco_widget_node Node(uint32_t kind, uint32_t id, uint32_t parent = 0) {
    sco_widget_node n{}; n.size = sizeof(n); n.kind = kind; n.id = id; n.parent = parent; n.label = "Name"; return n;
}
static sco_widget_node_1_1 Node11(uint32_t kind, uint32_t id, uint32_t parent = 0) {
    sco_widget_node_1_1 n{}; n.base = Node(kind, id, parent); n.base.size = sizeof(n); return n;
}
static sco_result SetTree(const std::vector<sco_widget_node>& v, sco_widget_event_fn fn = nullptr, void* ctx = nullptr) {
    return table->set_panel(alpha, "alpha.main", "Title", v.data(), static_cast<uint32_t>(v.size()), sizeof(sco_widget_node), fn, ctx);
}
static sco_result Update(const std::vector<sco_widget_node>& v, const char* id = "alpha.main") {
    return table->update_nodes(alpha, id, v.data(), static_cast<uint32_t>(v.size()), sizeof(sco_widget_node));
}
static std::vector<uint32_t> Ids() {
    std::vector<uint32_t> out;
    for (const auto& p : W::Panels()) if (p.id == "alpha.main") for (const auto& n : p.nodes) out.push_back(n.id);
    return out;
}
static std::vector<uint32_t> Parents() {
    std::vector<uint32_t> out;
    for (const auto& p : W::Panels()) if (p.id == "alpha.main") for (const auto& n : p.nodes) out.push_back(n.parent);
    return out;
}

static sco_widget_node_1_2 Node12(uint32_t kind, uint32_t id, const char* tag = nullptr, uint32_t parent = 0) {
    sco_widget_node_1_2 n{};
    n.base = Node11(kind, id, parent);
    n.base.base.size = sizeof(n);
    n.tag = tag;
    return n;
}
static sco_result Set12(const std::vector<sco_widget_node_1_2>& v, const char* id = "alpha.main") {
    return table->set_panel(alpha, id, "Title", reinterpret_cast<const sco_widget_node*>(v.data()),
                             static_cast<uint32_t>(v.size()), sizeof(sco_widget_node_1_2), nullptr, nullptr);
}
static std::vector<std::string> TagList() {
    std::vector<std::string> out;
    for (const auto& p : W::Panels()) if (p.id == "alpha.main") for (const auto& n : p.nodes) out.push_back(n.tag);
    return out;
}
// 1.2 stable tags: round-trip, panel uniqueness (set and merged update), validation,
// and byte-for-byte 1.1 clients on the same host.
static void TestStableTags() {
    static const std::string longest(63, 't');
    std::vector<sco_widget_node_1_2> tree(3);
    tree[0] = Node12(SCO_WIDGET_ROW, 1);
    tree[1] = Node12(SCO_WIDGET_BUTTON, 2, "open.bay", 1);
    tree[2] = Node12(SCO_WIDGET_BUTTON, 3, longest.c_str(), 1);
    CHECK(Set12(tree) == SCO_OK);
    auto tags = TagList();
    CHECK(tags.size() == 3 && tags[0].empty() && tags[1] == "open.bay" && tags[2] == longest);
    tree[2] = Node12(SCO_WIDGET_BUTTON, 3, "open.bay", 1);
    CHECK(Set12(tree) == SCO_BAD_ARG);   // duplicate inside one tree
    CHECK(TagList()[2] == longest);      // panel untouched
    tree[2] = Node12(SCO_WIDGET_BUTTON, 3, "bad tag!", 1);
    CHECK(Set12(tree) == SCO_BAD_ARG);   // charset
    const std::string tooLong(64, 't');
    tree[2] = Node12(SCO_WIDGET_BUTTON, 3, tooLong.c_str(), 1);
    CHECK(Set12(tree) == SCO_BAD_ARG);   // 64 bytes
    tree[2] = Node12(SCO_WIDGET_BUTTON, 3, nullptr, 1);
    CHECK(Set12(tree) == SCO_OK);
    CHECK(TagList()[2].empty());
    // Merged update: a fragment tag clashing with a kept node outside the subtree.
    tree[2] = Node12(SCO_WIDGET_BUTTON, 3, longest.c_str(), 1);
    CHECK(Set12(tree) == SCO_OK);
    std::vector<sco_widget_node_1_2> frag(1);
    frag[0] = Node12(SCO_WIDGET_BUTTON, 2, longest.c_str(), 1);
    CHECK(table->update_nodes(alpha, "alpha.main", reinterpret_cast<const sco_widget_node*>(frag.data()),
                              1, sizeof(frag[0])) == SCO_BAD_ARG);
    CHECK(TagList()[1] == "open.bay" && TagList()[2] == longest);
    frag[0] = Node12(SCO_WIDGET_BUTTON, 2, "shut.bay", 1);
    CHECK(table->update_nodes(alpha, "alpha.main", reinterpret_cast<const sco_widget_node*>(frag.data()),
                              1, sizeof(frag[0])) == SCO_OK);
    CHECK(TagList()[1] == "shut.bay");
    // A 1.1 plugin on the same host: 104-byte nodes keep working and carry no tag.
    std::vector<sco_widget_node_1_1> legacy(2);
    legacy[0] = Node11(SCO_WIDGET_ROW, 1);
    legacy[1] = Node11(SCO_WIDGET_BUTTON, 2, 1);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", reinterpret_cast<const sco_widget_node*>(legacy.data()),
                           2, sizeof(sco_widget_node_1_1), nullptr, nullptr) == SCO_OK);
    CHECK(TagList().size() == 2 && TagList()[1].empty());
    // Malformed tails are refused: nonzero pad.
    std::vector<sco_widget_node_1_2> dirty(1);
    dirty[0] = Node12(SCO_WIDGET_LABEL, 1, "ok.tag");
    dirty[0]._reserved[1] = 1;
    CHECK(Set12(dirty, "alpha.dirty") == SCO_BAD_ARG);
}
struct Received10 { std::vector<Event10> events; };
static void On10(const char* id, const Event10* event, void* ctx) {
    CHECK(std::strcmp(id, "alpha.old") == 0);
    static_cast<Received10*>(ctx)->events.push_back(*event);
}
// A plugin built against the 1.0 header keeps working, byte for byte.
static void TestFrozen10Consumer(const sco_api* api) {
    const void* out = nullptr;
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &out) == SCO_OK && out);
    const auto* t10 = static_cast<const Table10*>(out);
    CHECK(t10->size >= offsetof(Table10, remove_panel) + sizeof(t10->remove_panel) && t10->set_panel && t10->remove_panel);
    const char* items[]{"A", "B", "C"};
    Node10 nodes[2]{};
    nodes[0].size = sizeof(Node10); nodes[0].kind = SCO_WIDGET_BUTTON; nodes[0].id = 1; nodes[0].label = "Go";
    nodes[1].size = sizeof(Node10); nodes[1].kind = SCO_WIDGET_LIST; nodes[1].id = 2; nodes[1].label = "Pick";
    nodes[1].items = items; nodes[1].item_count = 3; nodes[1].index = -1;
    Received10 got;
    CHECK(t10->set_panel(alpha, "alpha.old", "Old", nodes, 2, sizeof(Node10), On10, &got) == SCO_OK);
    HeadlessRenderer r;
    r.candidates = {{1, SCO_WIDGET_BUTTON}, {2, SCO_WIDGET_LIST, 2}, W::Event{2, SCO_WIDGET_EVENT_PAGE, 0, 0, {}, 1}};
    CHECK(W::RenderPanel("alpha.old", r) == Result::Ok);
    CHECK(got.events.size() == 2); // a PAGE candidate never reaches a 1.0 panel
    for (const auto& e : got.events) CHECK(e.size == sizeof(Event10));
    CHECK(got.events.size() == 2 && got.events[1].kind == SCO_WIDGET_LIST && got.events[1].index == 2);
    CHECK(t10->remove_panel(alpha, "alpha.old") == SCO_OK);
}
static void TestTable(const sco_api* api) {
    const void* out = nullptr;
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_1, &out) == SCO_OK && out == W::Table());
    table = W::Table();
    CHECK(table->size == sizeof(sco_widgets_v1) && table->size == 48);
    CHECK(table->update_nodes && table->remove_nodes && table->capabilities);
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_2, &out) == SCO_OK && out == W::Table());
}
static void TestCapabilities() {
    uint32_t caps[12]{};
    caps[SCO_WIDGETS_CAP_COUNT] = 12;
    CHECK(table->capabilities(alpha, caps) == SCO_OK && caps[SCO_WIDGETS_CAP_COUNT] == SCO_WIDGETS_CAPS_1_2);
    CHECK((caps[SCO_WIDGETS_CAP_FEATURES] & 0xffu) == 0xffu);
    CHECK(caps[SCO_WIDGETS_CAP_MAX_PANELS] == SCO_WIDGETS_MAX_PANELS && caps[SCO_WIDGETS_CAP_MAX_NODES] == SCO_WIDGETS_MAX_NODES);
    CHECK(caps[SCO_WIDGETS_CAP_MAX_TOTAL_NODES] == SCO_WIDGETS_MAX_TOTAL_NODES && caps[SCO_WIDGETS_CAP_MAX_ITEMS] == SCO_WIDGETS_MAX_ITEMS);
    CHECK(caps[SCO_WIDGETS_CAP_MAX_COLUMNS] == SCO_WIDGETS_MAX_COLUMNS && caps[SCO_WIDGETS_CAP_MAX_TEXT] == SCO_WIDGETS_MAX_TEXT);
    CHECK(caps[SCO_WIDGETS_CAP_MAX_EVENTS] == SCO_WIDGETS_MAX_EVENTS && caps[SCO_WIDGETS_CAP_MAX_TAG] == SCO_WIDGETS_MAX_TAG && caps[10] == 0 && caps[11] == 0);
    uint32_t shortCaps[4]{4, 0xdead, 0xdead, 0xdead};
    CHECK(table->capabilities(alpha, shortCaps) == SCO_OK && shortCaps[0] == 4 && shortCaps[3] == SCO_WIDGETS_MAX_NODES);
    uint32_t tiny[2]{1, 0xdead};
    CHECK(table->capabilities(alpha, tiny) == SCO_BAD_ARG && tiny[0] == 1 && tiny[1] == 0xdead);
    CHECK(table->capabilities(alpha, nullptr) == SCO_BAD_ARG);
    CHECK(table->capabilities(nullptr, caps) == SCO_BAD_ARG);
    sco_result off = SCO_OK;
    std::thread([&] { off = table->capabilities(alpha, caps); }).join();
    CHECK(off == SCO_WRONG_THREAD);
}
static void TestUpdateNodes() {
    // row(1){ label(2), col(3){ button(4) } }, label(5)
    std::vector<sco_widget_node> tree{Node(SCO_WIDGET_ROW, 1), Node(SCO_WIDGET_LABEL, 2, 1), Node(SCO_WIDGET_COLUMN, 3, 1),
                                      Node(SCO_WIDGET_BUTTON, 4, 3), Node(SCO_WIDGET_LABEL, 5)};
    CHECK(SetTree(tree) == SCO_OK);
    // An existing root id replaces its whole subtree at the same position.
    CHECK(Update({Node(SCO_WIDGET_COLUMN, 3, 1), Node(SCO_WIDGET_TOGGLE, 6, 3), Node(SCO_WIDGET_BUTTON, 7, 3)}) == SCO_OK);
    CHECK((Ids() == std::vector<uint32_t>{1, 2, 3, 6, 7, 5}));
    // Insert a new subtree as the last child of row(1), and a new root at the end.
    CHECK(Update({Node(SCO_WIDGET_LABEL, 8, 1)}) == SCO_OK);
    CHECK(Update({Node(SCO_WIDGET_ROW, 9), Node(SCO_WIDGET_LABEL, 10, 9)}) == SCO_OK);
    CHECK((Ids() == std::vector<uint32_t>{1, 2, 3, 6, 7, 8, 5, 9, 10}));
    CHECK((Parents() == std::vector<uint32_t>{0, 1, 1, 3, 3, 1, 0, 0, 9}));
    // Replacing a leaf with a different kind keeps the id and position.
    auto toggle = Node(SCO_WIDGET_TOGGLE, 2, 1); toggle.value = 1;
    CHECK(Update({toggle}) == SCO_OK && Ids()[1] == 2 && W::Panels()[0].nodes[1].kind == SCO_WIDGET_TOGGLE);
    const auto before = Ids();
    auto refused = [&](std::vector<sco_widget_node> v, sco_result want) {
        CHECK(Update(v) == want);
        CHECK(Ids() == before);
    };
    refused({Node(SCO_WIDGET_LABEL, 3)}, SCO_BAD_ARG);                                // moving a subtree
    refused({Node(SCO_WIDGET_LABEL, 11, 4)}, SCO_BAD_ARG);                            // parent no longer in the tree
    refused({Node(SCO_WIDGET_LABEL, 11, 5)}, SCO_BAD_ARG);                            // parent is a leaf
    refused({Node(SCO_WIDGET_LABEL, 11, 99)}, SCO_BAD_ARG);                           // absent parent
    refused({Node(SCO_WIDGET_LABEL, 11), Node(SCO_WIDGET_LABEL, 12)}, SCO_BAD_ARG);   // two roots
    refused({Node(SCO_WIDGET_ROW, 11), Node(SCO_WIDGET_LABEL, 5, 11)}, SCO_BAD_ARG);  // id used outside
    refused({Node(SCO_WIDGET_ROW, 11), Node(SCO_WIDGET_LABEL, 12, 11), Node(SCO_WIDGET_LABEL, 12, 11)}, SCO_BAD_ARG);
    refused({Node(SCO_WIDGET_LABEL, 0)}, SCO_BAD_ARG);
    auto bad = Node(SCO_WIDGET_BUTTON, 11); bad.label = ""; refused({bad}, SCO_BAD_ARG);
    // The replaced subtree's own ids may be reused inside the replacement.
    CHECK(Update({Node(SCO_WIDGET_COLUMN, 3, 1), Node(SCO_WIDGET_LABEL, 7, 3)}) == SCO_OK);
    CHECK((Ids() == std::vector<uint32_t>{1, 2, 3, 7, 8, 5, 9, 10}));
    // Per-panel limit counts the subtree being replaced.
    std::vector<sco_widget_node> many{Node(SCO_WIDGET_COLUMN, 1000)};
    for (uint32_t i = 1; i < 248; ++i) many.push_back(Node(SCO_WIDGET_LABEL, 1000 + i, 1000));
    CHECK(Ids().size() == 8 && many.size() == 248);
    CHECK(Update(many) == SCO_OK && W::Panels()[0].nodes.size() == 256);
    CHECK(Update({Node(SCO_WIDGET_LABEL, 2000)}) == SCO_TOO_MANY);
    many.pop_back();
    CHECK(Update(many) == SCO_OK && W::Panels()[0].nodes.size() == 255);
    // Argument and ownership errors.
    CHECK(Update({Node(SCO_WIDGET_LABEL, 3000)}, "alpha.absent") == SCO_NOT_FOUND);
    CHECK(Update({Node(SCO_WIDGET_LABEL, 3000)}, "beta.main") == SCO_BAD_ARG);
    CHECK(table->update_nodes(alpha, "alpha.main", nullptr, 1, sizeof(sco_widget_node)) == SCO_BAD_ARG);
    auto one = Node(SCO_WIDGET_LABEL, 3000);
    CHECK(table->update_nodes(alpha, "alpha.main", &one, 0, sizeof(one)) == SCO_BAD_ARG);
    CHECK(table->update_nodes(alpha, "alpha.main", &one, 1, sizeof(one) - 8) == SCO_BAD_ARG);
    sco_plugin* beta = sco::host::NewPlugin("beta");
    CHECK(table->update_nodes(beta, "alpha.main", &one, 1, sizeof(one)) == SCO_BAD_ARG);
    sco_result off = SCO_OK;
    std::thread([&] { off = Update({one}); }).join();
    CHECK(off == SCO_WRONG_THREAD);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
}
static void TestRemoveNodes() {
    std::vector<sco_widget_node> tree{Node(SCO_WIDGET_ROW, 1), Node(SCO_WIDGET_LABEL, 2, 1), Node(SCO_WIDGET_COLUMN, 3, 1),
                                      Node(SCO_WIDGET_BUTTON, 4, 3), Node(SCO_WIDGET_LABEL, 5)};
    CHECK(SetTree(tree) == SCO_OK);
    const uint32_t absent[]{2, 99};
    CHECK(table->remove_nodes(alpha, "alpha.main", absent, 2) == SCO_NOT_FOUND);
    CHECK(Ids().size() == 5); // atomic
    const uint32_t zero[]{0};
    CHECK(table->remove_nodes(alpha, "alpha.main", zero, 1) == SCO_BAD_ARG);
    const uint32_t all[]{1, 5};
    CHECK(table->remove_nodes(alpha, "alpha.main", all, 2) == SCO_BAD_ARG && Ids().size() == 5);
    const uint32_t nested[]{3, 4};
    CHECK(table->remove_nodes(alpha, "alpha.main", nested, 2) == SCO_OK);
    CHECK((Ids() == std::vector<uint32_t>{1, 2, 5}));
    const uint32_t row[]{1};
    CHECK(table->remove_nodes(alpha, "alpha.main", row, 1) == SCO_OK && (Ids() == std::vector<uint32_t>{5}));
    CHECK(table->remove_nodes(alpha, "alpha.main", nullptr, 1) == SCO_BAD_ARG);
    CHECK(table->remove_nodes(alpha, "alpha.main", row, 0) == SCO_BAD_ARG);
    CHECK(table->remove_nodes(alpha, "alpha.absent", row, 1) == SCO_NOT_FOUND);
    // The freed ids are usable again.
    CHECK(Update({Node(SCO_WIDGET_COLUMN, 1), Node(SCO_WIDGET_LABEL, 3, 1)}) == SCO_OK);
    CHECK((Ids() == std::vector<uint32_t>{5, 1, 3}));
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
}
static sco_result SetExt(const std::vector<sco_widget_node_1_1>& v, sco_widget_event_fn fn = nullptr, void* ctx = nullptr) {
    return table->set_panel(alpha, "alpha.main", "Title", &v[0].base, static_cast<uint32_t>(v.size()), sizeof(sco_widget_node_1_1), fn, ctx);
}
static void TestNodeExtension() {
    const char* items[]{"r0", "r1", "r2", "r3", "r4"};
    auto list = Node11(SCO_WIDGET_LIST, 1);
    list.base.items = items; list.base.item_count = 5; list.base.index = -1;
    list.visible_rows = 2; list.first_row = 4; list.severity = SCO_WIDGET_SEVERITY_WARNING;
    list.tooltip = "Pick one";
    CHECK(SetExt({list}) == SCO_OK);
    auto node = W::Panels()[0].nodes[0];
    CHECK(node.extended && node.severity == SCO_WIDGET_SEVERITY_WARNING && node.tooltip == "Pick one");
    CHECK(node.first_row == 4 && node.visible_rows == 2);
    auto reject = [&](sco_widget_node_1_1 bad, sco_result want) {
        CHECK(SetExt({bad}) == want);
        CHECK(W::Panels()[0].nodes[0].tooltip == "Pick one");
    };
    auto bad = list; bad.severity = 4; reject(bad, SCO_BAD_ARG);
    bad = list; bad._reserved = 1; reject(bad, SCO_BAD_ARG);
    bad = list; bad.first_row = 5; reject(bad, SCO_BAD_ARG);
    bad = list; bad.visible_rows = 0; reject(bad, SCO_BAD_ARG); // first_row without paging
    bad = list; bad.visible_rows = SCO_WIDGETS_MAX_ITEMS + 1; bad.first_row = 0; reject(bad, SCO_TOO_MANY);
    bad = list; bad.tooltip = "\xc0\xaf"; reject(bad, SCO_BAD_ARG);
    std::string longText(256, 'x');
    bad = list; bad.tooltip = longText.c_str(); reject(bad, SCO_TOO_MANY);
    auto button = Node11(SCO_WIDGET_BUTTON, 1); button.visible_rows = 1; reject(button, SCO_BAD_ARG);
    // TABLE rows are cell rows.
    const char* cells[]{"a", "b", "c", "d", "e", "f"};
    auto grid = Node11(SCO_WIDGET_TABLE, 1);
    grid.base.items = cells; grid.base.item_count = 6; grid.base.columns = 2; grid.visible_rows = 1; grid.first_row = 2;
    CHECK(SetExt({grid}) == SCO_OK);
    grid.first_row = 3; CHECK(SetExt({grid}) == SCO_BAD_ARG);
    // A node below the 1.1 size is a 1.0 node: its partial tail is ignored.
    struct Partial { sco_widget_node node; uint64_t tail; } partial{Node(SCO_WIDGET_LABEL, 1), ~0ull};
    partial.node.size = sizeof(partial);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &partial.node, 1, sizeof(partial), nullptr, nullptr) == SCO_OK);
    CHECK(!W::Panels()[0].nodes[0].extended && W::Panels()[0].nodes[0].tooltip.empty());
    // 1.1 nodes through update_nodes, mixed with 1.0 nodes in one panel.
    CHECK(SetTree({Node(SCO_WIDGET_ROW, 1)}) == SCO_OK);
    auto tip = Node11(SCO_WIDGET_LABEL, 2, 1); tip.tooltip = "hint";
    CHECK(table->update_nodes(alpha, "alpha.main", &tip.base, 1, sizeof(tip)) == SCO_OK);
    CHECK(W::Panels()[0].nodes[1].tooltip == "hint");
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
}
struct Got {
    std::vector<sco_widget_event_1_1> events;
    std::vector<uint32_t> sizes;
    std::function<void()> action;
};
static void OnEvent(const char*, const sco_widget_event* e, void* ctx) {
    auto& g = *static_cast<Got*>(ctx);
    g.sizes.push_back(e->size);
    sco_widget_event_1_1 x{};
    x.base = *e; x.row = -2;
    if (e->size >= sizeof(sco_widget_event_1_1)) x = *reinterpret_cast<const sco_widget_event_1_1*>(e);
    g.events.push_back(x);
    if (g.action) g.action();
}
static void TestEventsAndPaging() {
    const char* items[]{"r0", "r1", "r2", "r3", "r4"};
    auto list = Node11(SCO_WIDGET_LIST, 1);
    list.base.items = items; list.base.item_count = 5; list.base.index = -1; list.visible_rows = 2;
    auto plain = Node11(SCO_WIDGET_LIST, 2);
    plain.base.items = items; plain.base.item_count = 5; plain.base.index = -1;
    auto button = Node11(SCO_WIDGET_BUTTON, 3);
    Got got;
    CHECK(SetExt({list, plain, button}, OnEvent, &got) == SCO_OK);
    HeadlessRenderer r;
    r.candidates = {W::Event{1, SCO_WIDGET_EVENT_PAGE, 0, 0, {}, 2}, W::Event{1, SCO_WIDGET_LIST, 3},
                    W::Event{3, SCO_WIDGET_BUTTON}, W::Event{2, SCO_WIDGET_EVENT_PAGE, 0, 0, {}, 1},
                    W::Event{1, SCO_WIDGET_EVENT_PAGE, 0, 0, {}, 5}, W::Event{1, SCO_WIDGET_EVENT_PAGE, 0, 0, {}, -1},
                    W::Event{1, SCO_WIDGET_EVENT_PAGE, 1, 0, {}, 1}, W::Event{1, SCO_WIDGET_EVENT_PAGE, 0, 0, "x", 1}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok);
    CHECK(got.events.size() == 3);
    for (uint32_t size : got.sizes) CHECK(size == sizeof(sco_widget_event_1_1));
    if (got.events.size() == 3) {
        CHECK(got.events[0].base.kind == SCO_WIDGET_EVENT_PAGE && got.events[0].row == 2 && got.events[0].base.node_id == 1);
        CHECK(got.events[0].base.index == 0 && got.events[0].base.value == 0 && !got.events[0].base.text);
        CHECK(got.events[1].base.kind == SCO_WIDGET_LIST && got.events[1].row == 3 && got.events[1].base.index == 3);
        CHECK(got.events[2].base.kind == SCO_WIDGET_BUTTON && got.events[2].row == -1);
    }
    const auto node = W::Panels()[0].nodes[0];
    CHECK(node.first_row == 2 && node.index == 3);
    // Disabled paged lists do not page.
    got = {}; list.base.flags = SCO_WIDGET_DISABLED;
    CHECK(SetExt({list}, OnEvent, &got) == SCO_OK);
    r.candidates = {W::Event{1, SCO_WIDGET_EVENT_PAGE, 0, 0, {}, 2}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && got.events.empty());
    // update_nodes from a callback drops the rest of that pass, like set_panel.
    list.base.flags = 0; got = {};
    CHECK(SetExt({list, button}, OnEvent, &got) == SCO_OK);
    got.action = [&] { CHECK(Update({Node(SCO_WIDGET_LABEL, 9)}) == SCO_OK); };
    r.candidates = {W::Event{3, SCO_WIDGET_BUTTON}, W::Event{3, SCO_WIDGET_BUTTON}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && got.events.size() == 1);
    got.action = [&] { const uint32_t ids[]{9}; CHECK(table->remove_nodes(alpha, "alpha.main", ids, 1) == SCO_OK); };
    got.events.clear();
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && got.events.size() == 1);
    got.action = {};
    // Once the last 1.1 node is gone, events return to the 1.0 shape.
    CHECK(Update({Node(SCO_WIDGET_BUTTON, 3)}) == SCO_OK);
    const uint32_t ids[]{1};
    CHECK(table->remove_nodes(alpha, "alpha.main", ids, 1) == SCO_OK);
    got.sizes.clear(); r.candidates = {W::Event{3, SCO_WIDGET_BUTTON}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && got.sizes.size() == 1 && got.sizes[0] == sizeof(sco_widget_event));
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
}
static void TestCoalescing() {
    CHECK(SetTree({Node(SCO_WIDGET_COLUMN, 1), Node(SCO_WIDGET_TOGGLE, 2, 1), Node(SCO_WIDGET_BUTTON, 3, 1)}) == SCO_OK);
    HeadlessRenderer r;
    uint64_t builds = W::SnapshotBuilds();
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && W::SnapshotBuilds() == builds + 1);
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && W::SnapshotBuilds() == builds + 1); // unchanged: reused
    for (uint32_t i = 10; i < 30; ++i) CHECK(Update({Node(SCO_WIDGET_LABEL, i, 1)}) == SCO_OK);
    const uint32_t ids[]{10, 11};
    CHECK(table->remove_nodes(alpha, "alpha.main", ids, 2) == SCO_OK);
    builds = W::SnapshotBuilds();
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && W::SnapshotBuilds() == builds + 1);
    CHECK(r.recorded.back().nodes.size() == 21);
    // Button events change no state; a toggle edit does, and is rebuilt once.
    r.candidates = {W::Event{3, SCO_WIDGET_BUTTON}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && W::SnapshotBuilds() == builds + 1);
    r.candidates = {W::Event{2, SCO_WIDGET_TOGGLE, 0, 1}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && W::SnapshotBuilds() == builds + 1);
    r.candidates.clear();
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && W::SnapshotBuilds() == builds + 2);
    CHECK(r.recorded.back().nodes[1].value == 1);
    // A snapshot survives replacement and removal during its own pass.
    struct Mutating : HeadlessRenderer {
        std::function<void()> action;
        void Render(const W::Panel& p, std::vector<W::Event>& e) override { if (action) action(); HeadlessRenderer::Render(p, e); }
    } m;
    m.action = [] { CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK); };
    CHECK(W::RenderPanel("alpha.main", m) == Result::Ok && m.recorded.back().nodes.size() == 21);
    CHECK(W::RenderPanel("alpha.main", m) == Result::NotFound);
}
int main() {
    sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"test-widgets11 1.0"});
    alpha = sco::host::NewPlugin("alpha");
    CHECK(W::Start() == Result::Ok);
    TestTable(api);
    TestFrozen10Consumer(api);
    TestCapabilities();
    TestUpdateNodes();
    TestRemoveNodes();
    TestNodeExtension();
    TestStableTags();
    TestEventsAndPaging();
    TestCoalescing();
    W::Stop();
    CHECK(table->update_nodes(alpha, "alpha.main", nullptr, 0, 0) == SCO_UNAVAILABLE);
    CHECK(table->capabilities(alpha, nullptr) == SCO_UNAVAILABLE);
    std::printf("widgets 1.1: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
