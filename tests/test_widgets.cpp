// SPDX-License-Identifier: GPL-3.0-only
#include "sco/widgets.h"
#include "sco/host.h"
#include "../src/api/internal.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <limits>
#include <array>
#include <functional>

namespace W = sco::widgets;
using sco::Result;
static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
struct HeadlessRenderer : W::Renderer {
    std::vector<W::Panel> recorded;
    std::vector<W::Event> candidates;
    bool inside = false;
    void Render(const W::Panel& panel, std::vector<W::Event>& events) override {
        inside = true;
        recorded.push_back(panel); // records every node, owned strings and item arrays
        events = candidates;
        inside = false;
    }
};
static sco_widget_node Node(uint32_t kind, uint32_t id = 1) {
    sco_widget_node n{}; n.size = sizeof(n); n.kind = kind; n.id = id; n.label = "Name"; return n;
}
static const sco_widgets_v1* table = nullptr;
static sco_plugin* alpha = nullptr;
static void TestLifecycle(const sco_api* api) {
    const void* out = nullptr;
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &out) == SCO_NOT_FOUND);
    CHECK(W::Start() == Result::Ok);
    CHECK(W::Start() == Result::BadArg);
    CHECK(W::Started());
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &out) == SCO_OK);
    CHECK(out == W::Table()); table = W::Table();
    CHECK(table->size == sizeof(sco_widgets_v1) && table->_pad == 0);
    auto n = Node(SCO_WIDGET_LABEL);
    char title[] = "Title", label[] = "Copied"; n.label = label;
    CHECK(table->set_panel(alpha, "alpha.main", title, &n, 1, sizeof(n), nullptr, nullptr) == SCO_OK);
    title[0] = label[0] = 'X';
    auto panels = W::Panels();
    CHECK(panels.size() == 1 && panels[0].title == "Title" && panels[0].owner == "alpha");
    CHECK(panels[0].nodes[0].label == "Copied");
    HeadlessRenderer renderer;
    CHECK(W::RenderPanel("alpha.main", renderer) == Result::Ok);
    CHECK(renderer.recorded.size() == 1 && renderer.recorded[0].nodes.size() == 1);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_NOT_FOUND);
    CHECK(W::RenderPanel("alpha.main", renderer) == Result::NotFound);
    W::Stop(); CHECK(!W::Started() && W::Panels().empty());
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &n, 1, sizeof(n), nullptr, nullptr) == SCO_UNAVAILABLE);
    CHECK(W::RenderPanel("alpha.main", renderer) == Result::Unavailable);
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &out) == SCO_NOT_FOUND);
    W::Stop(); CHECK(W::Start() == Result::Ok);
}
static sco_result Set(const sco_widget_node& n, const char* id = "alpha.main") {
    return table->set_panel(alpha, id, "Title", &n, 1, sizeof(n), nullptr, nullptr);
}
static void TestValidation() {
    const auto good = Node(SCO_WIDGET_BUTTON);
    CHECK(Set(good) == SCO_OK);
    auto bad = good; bad.flags = 2;
    CHECK(Set(bad) == SCO_BAD_ARG);
    CHECK(W::Panels()[0].nodes[0].flags == 0); // failed replacement is atomic
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
}
static void TestKindsAndBadData() {
    const char* items[] = {"One", "Two"};
    for (uint32_t kind = 1; kind <= 10; ++kind) {
        auto n = Node(kind);
        if (kind == SCO_WIDGET_SLIDER) { n.minimum = -1; n.maximum = 1; n.value = 0.5; }
        if (kind == SCO_WIDGET_COMBO || kind == SCO_WIDGET_LIST || kind == SCO_WIDGET_TABLE) {
            n.items = items; n.item_count = 2; n.index = -1;
            if (kind == SCO_WIDGET_TABLE) n.columns = 2;
        }
        if (kind == SCO_WIDGET_TEXT_INPUT) n.text = "Value";
        CHECK(Set(n) == SCO_OK);
        HeadlessRenderer renderer;
        CHECK(W::RenderPanel("alpha.main", renderer) == Result::Ok);
        CHECK(renderer.recorded[0].nodes[0].kind == kind);
    }
    auto good = Node(SCO_WIDGET_BUTTON);
    CHECK(Set(good) == SCO_OK);
    auto reject = [&](sco_widget_node bad) {
        CHECK(Set(bad) == SCO_BAD_ARG);
        CHECK(W::Panels()[0].nodes[0].kind == SCO_WIDGET_BUTTON);
    };
    auto bad = good; bad.kind = 0; reject(bad);
    bad = good; bad.kind = 11; reject(bad);
    bad = good; bad.id = 0; reject(bad);
    bad = good; bad.parent = 123; reject(bad);
    bad = good; bad.size = sizeof(bad) - 1; reject(bad);
    bad = good; bad.size = sizeof(bad) + 1; reject(bad);
    bad = good; bad.label = ""; reject(bad);
    bad = good; bad.label = nullptr; reject(bad);
    bad = good; bad.label = "\xc0\xaf"; reject(bad);
    bad = good; bad.label = "\xed\xa0\x80"; reject(bad);
    bad = good; bad.label = "\xf4\x90\x80\x80"; reject(bad);
    bad = good; bad.label = "\xe2\x82"; reject(bad);
    bad = good; bad.text = "unused"; reject(bad);
    bad = good; bad.value = 1; reject(bad);
    bad = good; bad.minimum = 1; reject(bad);
    bad = good; bad.maximum = 1; reject(bad);
    bad = good; bad.value = std::numeric_limits<double>::quiet_NaN(); reject(bad);
    bad = good; bad.index = 1; reject(bad);
    bad = good; bad.items = items; reject(bad);
    bad = good; bad.columns = 1; reject(bad);
    bad = Node(SCO_WIDGET_TOGGLE); bad.value = 2; reject(bad);
    bad = Node(SCO_WIDGET_TOGGLE); bad.minimum = 1; reject(bad);
    bad = Node(SCO_WIDGET_SLIDER); bad.minimum = 2; bad.maximum = 1; reject(bad);
    bad = Node(SCO_WIDGET_SLIDER); bad.maximum = 1; bad.value = 2; reject(bad);
    bad = Node(SCO_WIDGET_SLIDER); bad.maximum = std::numeric_limits<double>::infinity(); reject(bad);
    bad = Node(SCO_WIDGET_COMBO); bad.items = items; bad.item_count = 2; bad.index = 2; reject(bad);
    bad.index = -2; reject(bad);
    bad = Node(SCO_WIDGET_TABLE); bad.items = items; bad.item_count = 2; bad.columns = 3; reject(bad);
    bad.columns = 0; reject(bad);
    bad = Node(SCO_WIDGET_TEXT_INPUT); bad.value = 1; reject(bad);
    for (const char* id : {"beta.main", "alpha", "alpha.", ".alpha.x", "alpha..x", "Alpha.x", "alpha.x-y", "alphax.main", ""}) {
        CHECK(Set(good, id) == SCO_BAD_ARG);
        CHECK(table->remove_panel(alpha, id) == SCO_BAD_ARG);
    }
    CHECK(Set(good, nullptr) == SCO_BAD_ARG);
    CHECK(table->set_panel(nullptr, "alpha.main", "Title", &good, 1, sizeof(good), nullptr, nullptr) == SCO_BAD_ARG);
    int bogus = 0;
    CHECK(table->set_panel(reinterpret_cast<sco_plugin*>(&bogus), "alpha.main", "Title", &good, 1, sizeof(good), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->set_panel(alpha, "alpha.main", "", &good, 1, sizeof(good), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->set_panel(alpha, "alpha.main", "\xff", &good, 1, sizeof(good), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
}
static void TestTreesQuotasAndStrides() {
    auto root = Node(SCO_WIDGET_ROW, 1), col = Node(SCO_WIDGET_COLUMN, 2), leaf = Node(SCO_WIDGET_LABEL, 3);
    col.parent = 1; leaf.parent = 2;
    std::vector<sco_widget_node> tree{root, col, leaf, Node(SCO_WIDGET_BUTTON, 4)};
    auto setTree = [&](const std::vector<sco_widget_node>& v) {
        return table->set_panel(alpha, "alpha.main", "Title", v.data(), static_cast<uint32_t>(v.size()), sizeof(root), nullptr, nullptr);
    };
    CHECK(setTree(tree) == SCO_OK);
    HeadlessRenderer renderer; CHECK(W::RenderPanel("alpha.main", renderer) == Result::Ok);
    CHECK(renderer.recorded[0].nodes.size() == 4 && renderer.recorded[0].nodes[2].parent == 2);
    auto badTree = tree; badTree[3].parent = 3; CHECK(setTree(badTree) == SCO_BAD_ARG); // leaf parent
    badTree = tree; badTree[0].parent = 2; CHECK(setTree(badTree) == SCO_BAD_ARG); // forward parent
    badTree = tree; badTree[3].id = 2; CHECK(setTree(badTree) == SCO_BAD_ARG); // duplicate
    badTree = tree; auto late = Node(SCO_WIDGET_LABEL, 5); late.parent = 2; badTree.push_back(late);
    CHECK(setTree(badTree) == SCO_BAD_ARG); // subtree reopened after a root sibling
    // Complete advertised extents, including a physically truncated allocation.
    alignas(sco_widget_node) uint32_t truncated = sizeof(uint32_t);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", reinterpret_cast<sco_widget_node*>(&truncated), 1, sizeof(root), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &root, 1, sizeof(root)-1, nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &root, 1, sizeof(root)+1, nullptr, nullptr) == SCO_BAD_ARG);
    alignas(sco_widget_node) std::array<char, sizeof(root)+8> misaligned{};
    CHECK(table->set_panel(alpha, "alpha.main", "Title", reinterpret_cast<sco_widget_node*>(misaligned.data()+1), 1, sizeof(root), nullptr, nullptr) == SCO_BAD_ARG);
    struct Extended { sco_widget_node node; uint64_t tail; } ext{root, 0x1234};
    ext.node.size = sizeof(ext);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &ext.node, 1, sizeof(ext), nullptr, nullptr) == SCO_OK);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", nullptr, 1, sizeof(root), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &root, 0, sizeof(root), nullptr, nullptr) == SCO_BAD_ARG);
    std::vector<sco_widget_node> nodes;
    for (uint32_t i = 1; i <= 257; ++i) nodes.push_back(Node(SCO_WIDGET_LABEL, i));
    CHECK(setTree(nodes) == SCO_TOO_MANY);
    nodes.pop_back(); CHECK(setTree(nodes) == SCO_OK);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
    // Panel and global node quotas account for replacements and removals.
    for (unsigned i = 0; i < 16; ++i) {
        std::string id = "alpha.p" + std::to_string(i);
        CHECK(table->set_panel(alpha, id.c_str(), "Title", nodes.data(), 256, sizeof(root), nullptr, nullptr) == SCO_OK);
    }
    CHECK(Set(root, "alpha.overflow") == SCO_TOO_MANY);
    CHECK(table->set_panel(alpha, "alpha.p0", "Replace", nodes.data(), 256, sizeof(root), nullptr, nullptr) == SCO_OK);
    CHECK(Set(root, "alpha.p0") == SCO_OK);
    CHECK(Set(root, "alpha.overflow") == SCO_OK);
    for (const auto& p : W::Panels()) CHECK(table->remove_panel(alpha, p.id.c_str()) == SCO_OK);
    for (unsigned i = 0; i < 64; ++i) CHECK(Set(root, ("alpha.q" + std::to_string(i)).c_str()) == SCO_OK);
    CHECK(Set(root, "alpha.extra") == SCO_TOO_MANY);
    CHECK(Set(root, "alpha.q0") == SCO_OK); // replacement at panel limit
    for (const auto& p : W::Panels()) CHECK(table->remove_panel(alpha, p.id.c_str()) == SCO_OK);
    std::string maxText(255, 'x'), longText(256, 'x');
    auto text = Node(SCO_WIDGET_TEXT_INPUT); text.text = maxText.c_str(); CHECK(Set(text) == SCO_OK);
    text.text = longText.c_str(); CHECK(Set(text) == SCO_TOO_MANY);
    text = Node(SCO_WIDGET_LABEL); text.label = longText.c_str(); CHECK(Set(text) == SCO_TOO_MANY);
    text.label = maxText.c_str(); CHECK(Set(text) == SCO_OK);
    std::string maxId = "alpha." + std::string(57, 'x'); CHECK(Set(root, maxId.c_str()) == SCO_OK);
    CHECK(Set(root, (maxId + "x").c_str()) == SCO_BAD_ARG);
    CHECK(table->set_panel(alpha, "alpha.main", std::string(63, 'x').c_str(), &root, 1, sizeof(root), nullptr, nullptr) == SCO_OK);
    CHECK(table->set_panel(alpha, "alpha.main", std::string(64, 'x').c_str(), &root, 1, sizeof(root), nullptr, nullptr) == SCO_BAD_ARG);
    std::vector<const char*> items(257, "Cell");
    auto list = Node(SCO_WIDGET_LIST); list.items = items.data(); list.item_count = 257; CHECK(Set(list) == SCO_TOO_MANY);
    list.item_count = 256; CHECK(Set(list) == SCO_OK);
    items[0] = nullptr; CHECK(Set(list) == SCO_BAD_ARG);
    items[0] = "\xff"; CHECK(Set(list) == SCO_BAD_ARG);
    items[0] = longText.c_str(); CHECK(Set(list) == SCO_TOO_MANY);
    list.items = nullptr; CHECK(Set(list) == SCO_BAD_ARG);
    auto cells = Node(SCO_WIDGET_TABLE); cells.columns = 17; CHECK(Set(cells) == SCO_TOO_MANY);
    cells.columns = 16; CHECK(Set(cells) == SCO_OK);
    // Retained item copies survive plugin backing storage changes.
    char option[] = "Original", input[] = "Input"; const char* copiedItems[]{option};
    list.items = copiedItems; list.item_count = 1;
    CHECK(Set(list) == SCO_OK); option[0] = 'X'; copiedItems[0] = "Other";
    CHECK(W::Panels()[0].nodes[0].items[0] == "Original");
    text = Node(SCO_WIDGET_TEXT_INPUT); text.text = input; CHECK(Set(text) == SCO_OK); input[0] = 'X';
    CHECK(W::Panels()[0].nodes[0].text == "Input");
    for (const auto& p : W::Panels()) CHECK(table->remove_panel(alpha, p.id.c_str()) == SCO_OK);
}
struct EventContext {
    HeadlessRenderer* renderer = nullptr;
    std::vector<W::Event> received;
    std::function<void(const char*, const sco_widget_event*)> action;
};
static void OnEvent(const char* id, const sco_widget_event* event, void* ctx) {
    auto& c = *static_cast<EventContext*>(ctx);
    CHECK(!c.renderer->inside && std::strcmp(id, "alpha.main") == 0);
    CHECK(event->size == sizeof(*event));
    c.received.push_back({event->node_id, event->kind, event->index, event->value, event->text ? event->text : ""});
    for (const auto& p : W::Panels()) if (p.id == id) for (const auto& n : p.nodes) if (n.id == event->node_id) {
        if (event->kind == SCO_WIDGET_TOGGLE || event->kind == SCO_WIDGET_SLIDER) CHECK(n.value == event->value);
        if (event->kind == SCO_WIDGET_COMBO || event->kind == SCO_WIDGET_LIST) CHECK(n.index == event->index);
        if (event->kind == SCO_WIDGET_TEXT_INPUT) CHECK(n.text == event->text);
    }
    if (c.action) c.action(id, event);
}
static void TestEvents() {
    const char* items[]{"A", "B"};
    std::vector<sco_widget_node> nodes;
    for (uint32_t k = 1; k <= 10; ++k) {
        auto n = Node(k, k);
        if (k == SCO_WIDGET_SLIDER) { n.minimum = -2; n.maximum = 2; }
        if (k == SCO_WIDGET_COMBO || k == SCO_WIDGET_LIST || k == SCO_WIDGET_TABLE) { n.items = items; n.item_count = 2; if (k == SCO_WIDGET_TABLE) n.columns = 2; }
        if (k == SCO_WIDGET_TEXT_INPUT) n.text = "Old";
        nodes.push_back(n);
    }
    HeadlessRenderer r; EventContext ctx{&r, {}, {}};
    auto set = [&] { return table->set_panel(alpha, "alpha.main", "Title", nodes.data(), static_cast<uint32_t>(nodes.size()), sizeof(sco_widget_node), OnEvent, &ctx); };
    CHECK(set() == SCO_OK);
    r.candidates = {{2,2,0,0,{}}, {3,3,0,1,{}}, {4,4,0,1.5,{}}, {5,5,1,0,{}}, {6,6,-1,0,{}}, {7,7,0,0,"New"}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok);
    CHECK(ctx.received.size() == 6);
    CHECK(r.recorded[0].nodes[6].text == "Old"); // snapshot was stable while rendering
    auto current = W::Panels()[0];
    CHECK(current.nodes[2].value == 1 && current.nodes[3].value == 1.5 && current.nodes[4].index == 1 && current.nodes[5].index == -1 && current.nodes[6].text == "New");
    // Every non-interactive kind and malformed payload is rejected.
    ctx.received.clear();
    r.candidates = {{1,1}, {8,8}, {9,9}, {10,10}, {999,2}, {2,3}, {2,2,1}, {2,2,0,1}, {2,2,0,0,"stray"},
                    {3,3,0,2}, {3,3,1,1}, {3,3,0,std::numeric_limits<double>::quiet_NaN()}, {4,4,0,3},
                    {4,4,0,std::numeric_limits<double>::infinity()}, {5,5,2}, {6,6,-2}, {5,5,0,1},
                    {7,7,0,0,std::string(256,'x')}, {7,7,0,0,"\xff"}, {7,7,0,0,std::string("x\0y",3)}, {7,7,1,0,"Text"}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && ctx.received.empty());
    nodes[1].flags = SCO_WIDGET_DISABLED; CHECK(set() == SCO_OK);
    r.candidates = {{2,2}}; CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && ctx.received.empty());
    nodes[1].flags = 0; CHECK(set() == SCO_OK);
    r.candidates.assign(257, W::Event{2,2});
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && ctx.received.size() == 256);
    // Callback replacement or removal must drop old pass's remaining events.
    ctx.received.clear(); r.candidates = {{2,2}, {3,3,0,1}};
    ctx.action = [&](const char*, const sco_widget_event*) { CHECK(set() == SCO_OK); };
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && ctx.received.size() == 1);
    ctx.received.clear();
    ctx.action = [&](const char* id, const sco_widget_event*) { CHECK(table->remove_panel(alpha, id) == SCO_OK); };
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && ctx.received.size() == 1);
    CHECK(W::Panels().empty());
    // Layout disablement is inherited, even for a misbehaving adapter.
    auto row = Node(SCO_WIDGET_ROW, 1), button = Node(SCO_WIDGET_BUTTON, 2);
    row.flags = SCO_WIDGET_DISABLED; button.parent = 1;
    const sco_widget_node disabledTree[]{row, button};
    ctx.action = {}; ctx.received.clear(); r.candidates = {{2,SCO_WIDGET_BUTTON}};
    CHECK(table->set_panel(alpha, "alpha.main", "Title", disabledTree, 2, sizeof(row), OnEvent, &ctx) == SCO_OK);
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && ctx.received.empty());
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
}
static sco_plugin* guardOwner = nullptr;
static std::string guardWhere;
static unsigned guardCalls = 0;
static bool CallGuard(const void* owner, const char* where, sco::TaskFn thunk, void* ctx) {
    ++guardCalls; CHECK(owner == guardOwner); guardWhere = where;
    CHECK(sco::detail::InCallout());
    try { thunk(ctx); return true; }
    catch (...) {
        CHECK(sco::detail::QuarantineOwner(owner) == Result::Ok);
        if (!sco::ServiceTableHandedOut(owner)) sco::detail::ReleaseFaultResources(owner);
        return false;
    }
}
struct MutatingRenderer : HeadlessRenderer {
    std::function<void()> action;
    void Render(const W::Panel& p, std::vector<W::Event>& events) override {
        HeadlessRenderer::Render(p, events);
        if (action) action();
    }
};
static void TestLifetimeAndThreads(const sco_api* api) {
    auto n = Node(SCO_WIDGET_BUTTON);
    HeadlessRenderer r; r.candidates = {{1,SCO_WIDGET_BUTTON}};
    EventContext ctx{&r, {}, {}};
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &n, 1, sizeof(n), OnEvent, &ctx) == SCO_OK);
    guardOwner = alpha; guardCalls = 0; sco::SetCalloutGuard(CallGuard);
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && guardCalls == 1 && guardWhere == "alpha.main");
    unsigned commandCalls = 0;
    sco_command command{}; command.size = sizeof(command); command.name = "alpha.test"; command.title = "Test";
    command.arg_def_size = sizeof(sco_arg_def); command.ctx = &commandCalls;
    command.fn = [](const sco_arg*, uint32_t, void* p, char*, uint32_t) -> sco_result { ++*static_cast<unsigned*>(p); return SCO_OK; };
    CHECK(api->register_command(alpha, &command) == SCO_OK);
    ctx.action = [&](const char*, const sco_widget_event*) { CHECK(api->invoke(alpha, "alpha.test", nullptr, 0, nullptr, nullptr) == SCO_OK); };
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && commandCalls == 1 && guardCalls == 3);
    ctx.action = {}; ctx.received.clear();
    sco::SetCalloutGuard(nullptr);
    sco_result setOff = SCO_OK, removeOff = SCO_OK; Result renderOff = Result::Ok, startOff = Result::Ok;
    std::thread([&] {
        setOff = Set(n); removeOff = table->remove_panel(alpha, "alpha.main");
        renderOff = W::RenderPanel("alpha.main", r); startOff = W::Start();
        CHECK(W::Panels().empty()); W::Stop();
    }).join();
    CHECK(setOff == SCO_WRONG_THREAD && removeOff == SCO_WRONG_THREAD && renderOff == Result::WrongThread && startOff == Result::WrongThread);
    CHECK(W::Started() && ctx.received.empty());
    sco_plugin* beta = sco::host::NewPlugin("beta");
    CHECK(table->set_panel(beta, "alpha.main", "Title", &n, 1, sizeof(n), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->remove_panel(beta, "alpha.main") == SCO_BAD_ARG);
    CHECK(table->remove_panel(beta, "beta.absent") == SCO_NOT_FOUND);
    CHECK(W::RenderPanel(nullptr, r) == Result::BadArg);
    // Adapter replacement after producing events cannot route them to a new tree.
    MutatingRenderer mutating; mutating.candidates = {{1,SCO_WIDGET_BUTTON}};
    mutating.action = [&] { CHECK(Set(n) == SCO_OK); };
    ctx.received.clear(); CHECK(W::RenderPanel("alpha.main", mutating) == Result::Ok && ctx.received.empty());
    // Null callback still retains edits.
    auto toggle = Node(SCO_WIDGET_TOGGLE); CHECK(Set(toggle) == SCO_OK);
    r.candidates = {{1,SCO_WIDGET_TOGGLE,0,1}};
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && W::Panels()[0].nodes[0].value == 1);
    // Adapter exceptions do not run events or change retained values.
    mutating.candidates = r.candidates; mutating.action = [] { throw 1; };
    CHECK(W::RenderPanel("alpha.main", mutating) == Result::Failed);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
    // Unload inside callback removes state and never calls freed ctx again.
    sco_plugin* unload = sco::host::NewPlugin("unload");
    unsigned unloadedCalls = 0;
    auto unloadFn = [](const char*, const sco_widget_event*, void* p) {
        auto* state = static_cast<std::pair<sco_plugin*, unsigned*>*>(p);
        ++*state->second; CHECK(sco::Release(state->first) == Result::Ok);
        delete state;
    };
    auto* state = new std::pair<sco_plugin*, unsigned*>{unload, &unloadedCalls};
    CHECK(table->set_panel(unload, "unload.main", "Unload", &n, 1, sizeof(n), unloadFn, state) == SCO_OK);
    r.candidates = {{1,SCO_WIDGET_BUTTON}, {1,SCO_WIDGET_BUTTON}};
    CHECK(W::RenderPanel("unload.main", r) == Result::Crashed && unloadedCalls == 1);
    CHECK(W::Panels().empty() && W::RenderPanel("unload.main", r) == Result::NotFound);
    CHECK(table->set_panel(unload, "unload.main", "Unload", &n, 1, sizeof(n), nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(table->remove_panel(unload, "unload.main") == SCO_BAD_ARG);
    // A provider whose raw table escaped is quarantined without resource cleanup.
    sco_plugin* retained = sco::host::NewPlugin("retained");
    const uint32_t backing = 123;
    CHECK(api->provide_service(retained, "retained.test", 0x10000, &backing) == SCO_OK);
    const void* tableOut = nullptr; CHECK(api->query_service("retained.test", 0x10000, &tableOut) == SCO_OK && tableOut == &backing);
    unsigned faultCalls = 0;
    auto faultFn = [](const char*, const sco_widget_event*, void* p) { ++*static_cast<unsigned*>(p); throw 1; };
    CHECK(table->set_panel(retained, "retained.main", "Retained", &n, 1, sizeof(n), faultFn, &faultCalls) == SCO_OK);
    guardOwner = retained; sco::SetCalloutGuard(CallGuard);
    CHECK(W::RenderPanel("retained.main", r) == Result::Crashed && faultCalls == 1);
    auto rendered = r.recorded.size();
    CHECK(W::RenderPanel("retained.main", r) == Result::Crashed && r.recorded.size() == rendered && faultCalls == 1);
    CHECK(W::Panels().empty()); // retained state is not a callable panel
    CHECK(table->set_panel(retained, "retained.main", "Retained", &n, 1, sizeof(n), faultFn, &faultCalls) == SCO_BAD_ARG);
    sco::SetCalloutGuard(nullptr);
    // A non-provider fault runs resource hooks; other owners are unaffected.
    sco_plugin* faulty = sco::host::NewPlugin("faulty");
    CHECK(table->set_panel(faulty, "faulty.main", "Faulty", &n, 1, sizeof(n), faultFn, &faultCalls) == SCO_OK);
    CHECK(Set(n) == SCO_OK);
    guardOwner = faulty; sco::SetCalloutGuard(CallGuard);
    CHECK(W::RenderPanel("faulty.main", r) == Result::Crashed && faultCalls == 2);
    CHECK(W::RenderPanel("faulty.main", r) == Result::NotFound);
    sco::SetCalloutGuard(nullptr);
    CHECK(W::Panels().size() == 1 && W::RenderPanel("alpha.main", r) == Result::Ok);
    // Shutdown barrier is enforced even through cached host tables.
    sco::detail::SetRuntimeStopping(true);
    CHECK(Set(n) == SCO_UNAVAILABLE && table->remove_panel(alpha, "alpha.main") == SCO_UNAVAILABLE);
    rendered = r.recorded.size();
    CHECK(W::RenderPanel("alpha.main", r) == Result::Unavailable && r.recorded.size() == rendered && W::Panels().empty());
    sco::detail::SetRuntimeStopping(false);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
    // Stop from a callback drops queued events, and new starts do not reuse generations.
    ctx.action = [&](const char*, const sco_widget_event*) { W::Stop(); CHECK(W::Start() == Result::Ok); CHECK(Set(n) == SCO_OK); };
    ctx.received.clear();
    CHECK(table->set_panel(alpha, "alpha.main", "Title", &n, 1, sizeof(n), OnEvent, &ctx) == SCO_OK);
    CHECK(W::RenderPanel("alpha.main", r) == Result::Ok && ctx.received.size() == 1);
    CHECK(table->remove_panel(alpha, "alpha.main") == SCO_OK);
    // Start rollback never withdraws another provider's pre-existing service.
    W::Stop();
    CHECK(sco::host::ProvideHostService(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &backing) == Result::Ok);
    CHECK(W::Start() == Result::BadArg && !W::Started());
    CHECK(api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &tableOut) == SCO_OK && tableOut == &backing);
    CHECK(sco::host::WithdrawHostService(SCO_WIDGETS_NAME) == Result::Ok);
    CHECK(W::Start() == Result::Ok);
    W::Stop();
    static const sco::ReleaseHook hooks[]{
        [](const void*) {}, [](const void*) {}, [](const void*) {}, [](const void*) {},
        [](const void*) {}, [](const void*) {}, [](const void*) {}, [](const void*) {}
    };
    for (auto hook : hooks) CHECK(sco::AddReleaseHook(hook) == Result::Ok);
    CHECK(W::Start() == Result::TooMany && !W::Started());
    for (auto hook : hooks) CHECK(sco::RemoveReleaseHook(hook) == Result::Ok);
    CHECK(W::Start() == Result::Ok);
}
int main() {
    sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"test-widgets 1.0"});
    alpha = sco::host::NewPlugin("alpha");
    TestLifecycle(api);
    TestValidation();
    TestKindsAndBadData();
    TestTreesQuotasAndStrides();
    TestEvents();
    TestLifetimeAndThreads(api);
    W::Stop();
    std::printf("widgets: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
