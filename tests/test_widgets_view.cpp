// SPDX-License-Identifier: GPL-3.0-only
// sco.widgets.view 1.0: the host-side flat reader and event queue (include/sco/widgets_view.h).
// Synthetic panels and a real Core; a second thread plays the menu thread. Nothing here runs in
// game: these are source, build and synthetic-thread results only.
#include "sco/widgets.h"
#include "sco/host.h"
#include "../src/api/internal.h"
#include "../src/widgets/view_internal.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
// tests/test_widgets_view_reader.c: a strict reader written in C against the header alone.
int scv_validate(const void* blob, uint32_t bytes);
const char* scv_str(const void* blob, sco_view_str s);
int scv_node(const void* blob, uint32_t index, sco_view_node* out);
int scv_item(const void* blob, uint32_t index, sco_view_str* out);
uint64_t scv_reference_key(uint32_t kind, uint32_t id, uint32_t parent, const char* label, size_t label_len,
                           const char* tag, size_t tag_len);
}

namespace W = sco::widgets;
namespace V = sco::widgets::viewimpl;
using sco::Result;

static std::atomic<int> passed{0}, failed{0};
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

static const sco_widgets_view_v1* view = nullptr;
static sco_view_client* client = nullptr;
static const sco_widgets_v1* table = nullptr;
static sco_plugin* alpha = nullptr;

// ---- the menu thread: runs closures one at a time on one thread, and waits for each ----
class MenuThread {
public:
    MenuThread() : thread_([this] { Loop(); }) {}
    ~MenuThread() {
        { std::lock_guard<std::mutex> hold(m_); quit_ = true; }
        cv_.notify_all();
        thread_.join();
    }
    void Run(std::function<void()> fn) {
        std::unique_lock<std::mutex> hold(m_);
        job_ = std::move(fn); has_ = true; finished_ = false;
        cv_.notify_all();
        cv_.wait(hold, [&] { return finished_; });
    }
private:
    void Loop() {
        std::unique_lock<std::mutex> hold(m_);
        for (;;) {
            cv_.wait(hold, [&] { return has_ || quit_; });
            if (quit_) return;
            auto fn = std::move(job_); has_ = false;
            hold.unlock(); fn(); hold.lock();
            finished_ = true; cv_.notify_all();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::function<void()> job_;
    bool has_ = false, quit_ = false, finished_ = false;
    std::thread thread_;
};

struct Blob {
    std::vector<uint64_t> mem;
    uint32_t bytes = 0;
    const void* data() const { return mem.data(); }
    const sco_view_blob_header& H() const { return *static_cast<const sco_view_blob_header*>(data()); }
    bool Node(uint32_t id, sco_view_node& out) const {
        for (uint32_t i = 0; i < H().node_count; ++i) {
            if (!scv_node(data(), i, &out)) return false;
            if (out.id == id) return true;
        }
        return false;
    }
    std::string Str(sco_view_str s) const { const char* p = scv_str(data(), s); return p ? std::string(p, s.length) : std::string("<bad>"); }
};

static sco_result CopyOn(MenuThread& t, const char* id, Blob& out) {
    sco_result r = SCO_OK;
    t.Run([&] {
        uint32_t need = 0;
        r = view->copy_panel(client, id, nullptr, &need);
        if (r != SCO_TOO_MANY) return;
        for (int tries = 0; tries < 16; ++tries) {
            out.mem.assign((need + 7) / 8 + 1, 0);
            uint32_t capacity = static_cast<uint32_t>(out.mem.size() * 8);
            r = view->copy_panel(client, id, out.mem.data(), &capacity);
            if (r == SCO_TOO_MANY) { need = capacity; continue; }
            if (r == SCO_OK) out.bytes = capacity;
            return;
        }
    });
    return r;
}
static sco_result SubmitOn(MenuThread& t, const char* id, const sco_view_event& e, uint64_t* seq) {
    sco_result r = SCO_OK;
    t.Run([&] { r = view->submit_event(client, id, &e, seq); });
    return r;
}
static sco_view_event EventFor(const Blob& b, uint32_t nodeId, uint32_t kind) {
    sco_view_event e{};
    e.size = sizeof(e); e.node_id = nodeId; e.kind = kind; e.row = -1; e.epoch = b.H().epoch;
    sco_view_node n{};
    if (b.Node(nodeId, n)) e.node_key = n.key;
    return e;
}
struct Stamps { uint64_t global = 0, list = 0, drops = 0, done = 0; };
static Stamps Read() {
    Stamps s;
    CHECK(view->stamps(client, &s.global, &s.list, &s.drops, &s.done) == SCO_OK);
    return s;
}
struct Drain { sco_result result = SCO_OK; uint32_t dispatched = 0, dropped = 0; };
static Drain DoDrain() {
    Drain d;
    d.result = view->dispatch_events(client, &d.dispatched, &d.dropped);
    return d;
}

// ---- a panel builder over the full 1.3 node layout ----
struct Spec {
    uint32_t kind = 0, id = 0, parent = 0, flags = 0;
    std::string label, tag, text, tooltip;
    double value = 0, minimum = 0, maximum = 0;
    int32_t index = 0;
    std::vector<std::string> items;
    uint32_t columns = 0, visibleRows = 0, firstRow = 0, severity = 0, presentation = 0;
};
static Spec Mk(uint32_t kind, uint32_t id, uint32_t parent, const std::string& label, const std::string& tag = "") {
    Spec s; s.kind = kind; s.id = id; s.parent = parent; s.label = label; s.tag = tag; return s;
}
struct Built {
    std::vector<sco_widget_node_1_3> nodes;
    std::vector<std::vector<const char*>> pointers;
};
static Built Build(const std::vector<Spec>& specs, size_t begin, size_t count) {
    Built b;
    b.nodes.resize(count);
    b.pointers.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const Spec& s = specs[begin + i];
        auto& x = b.nodes[i];
        x = sco_widget_node_1_3{};
        sco_widget_node& n = x.base.base.base;
        n.size = sizeof(sco_widget_node_1_3);
        n.kind = s.kind; n.id = s.id; n.parent = s.parent; n.flags = s.flags; n.index = s.index;
        n.label = s.label.c_str();
        n.text = s.kind == SCO_WIDGET_TEXT_INPUT ? s.text.c_str() : nullptr;
        n.value = s.value; n.minimum = s.minimum; n.maximum = s.maximum;
        n.columns = s.columns;
        for (const auto& item : s.items) b.pointers[i].push_back(item.c_str());
        n.items = b.pointers[i].empty() ? nullptr : b.pointers[i].data();
        n.item_count = static_cast<uint32_t>(b.pointers[i].size());
        x.base.base.tooltip = s.tooltip.empty() ? nullptr : s.tooltip.c_str();
        x.base.base.severity = s.severity;
        x.base.base.first_row = s.firstRow;
        x.base.base.visible_rows = s.visibleRows;
        x.base.tag = s.tag.empty() ? nullptr : s.tag.c_str();
        x.presentation = s.presentation;
    }
    return b;
}
static sco_result SetTree(sco_plugin* owner, const char* id, const std::vector<Spec>& specs,
                          sco_widget_event_fn fn = nullptr, void* ctx = nullptr, const char* title = "Title") {
    const Built b = Build(specs, 0, specs.size());
    return table->set_panel(owner, id, title, &b.nodes[0].base.base.base, static_cast<uint32_t>(b.nodes.size()),
                            sizeof(sco_widget_node_1_3), fn, ctx);
}
static sco_result UpdateTree(sco_plugin* owner, const char* id, const std::vector<Spec>& specs, size_t begin, size_t count) {
    const Built b = Build(specs, begin, count);
    return table->update_nodes(owner, id, &b.nodes[0].base.base.base, static_cast<uint32_t>(count),
                               sizeof(sco_widget_node_1_3));
}

// ---- a recording callback ----
struct Got { std::string panel; uint32_t id = 0, kind = 0; int32_t index = 0; double value = 0; std::string text, tag; int32_t row = -1; size_t size = 0; };
struct Rec {
    std::vector<Got> got;
    std::function<void(const Got&)> action;
};
static void OnEvent(const char* panel, const sco_widget_event* event, void* ctx) {
    auto& rec = *static_cast<Rec*>(ctx);
    Got g;
    g.panel = panel; g.id = event->node_id; g.kind = event->kind; g.index = event->index; g.value = event->value;
    g.text = event->text ? event->text : ""; g.size = event->size;
    if (event->size >= sizeof(sco_widget_event_1_1)) g.row = reinterpret_cast<const sco_widget_event_1_1*>(event)->row;
    if (event->size >= sizeof(sco_widget_event_1_4)) {
        const char* tag = reinterpret_cast<const sco_widget_event_1_4*>(event)->tag;
        g.tag = tag ? tag : "";
    }
    rec.got.push_back(g);
    if (rec.action) rec.action(g);
}
static bool LiveNode(const char* panel, uint32_t id, W::Node& out) {
    for (const auto& p : W::Panels()) if (p.id == panel)
        for (const auto& n : p.nodes) if (n.id == id) { out = n; return true; }
    return false;
}
static void RemoveAll() {
    for (const auto& p : W::Panels()) {
        sco_plugin* owner = p.owner == "alpha" ? alpha : nullptr;
        if (owner) table->remove_panel(owner, p.id.c_str());
    }
}

// =================================================================================================
static void TestHandshakeAndPins(const sco_api* api) {
    CHECK(view->size == sizeof(sco_widgets_view_v1) && view->version == SCO_WIDGETS_VIEW_VERSION_1_0);
    CHECK(view->capabilities && view->stamps && view->menu_thread_enter && view->menu_thread_leave &&
          view->list_panels && view->copy_panel && view->submit_event && view->dispatch_events);
    CHECK(V::ViewTable() == view && V::ViewClient() == client && V::ViewTable() == view);
    // The table is not a plugin service.
    const void* out = nullptr;
    CHECK(api->query_service("sco.widgets.view", SCO_WIDGETS_VIEW_VERSION_1_0, &out) == SCO_NOT_FOUND);
    CHECK(api->query_service("sco.widgets.view", 0x00000001u, &out) == SCO_NOT_FOUND);
    uint32_t caps[SCO_VIEW_CAPS_1_0 + 3]{};
    caps[0] = SCO_VIEW_CAPS_1_0 + 3;
    CHECK(view->capabilities(client, caps) == SCO_OK);
    CHECK(caps[SCO_VIEW_CAP_COUNT] == SCO_VIEW_CAPS_1_0);
    CHECK(caps[SCO_VIEW_CAP_FEATURES] == (SCO_VIEW_FEATURE_TAGS | SCO_VIEW_FEATURE_COALESCE | SCO_VIEW_FEATURE_HOST_PANELS));
    CHECK(caps[SCO_VIEW_CAP_MAX_PANELS] == SCO_WIDGETS_MAX_PANELS);
    CHECK(caps[SCO_VIEW_CAP_EVENT_QUEUE] == 256 && caps[SCO_VIEW_CAP_NODE_STRIDE] == sizeof(sco_view_node) &&
          caps[SCO_VIEW_CAP_ENTRY_STRIDE] == sizeof(sco_view_panel_entry));
    CHECK(caps[SCO_VIEW_CAPS_1_0] == 0 && caps[SCO_VIEW_CAPS_1_0 + 2] == 0); // nothing written past the count
    uint32_t shortCaps[4]{4, 0xdead, 0xdead, 0xdead};
    CHECK(view->capabilities(client, shortCaps) == SCO_OK && shortCaps[0] == 4 && shortCaps[1] == (SCO_VIEW_FEATURE_TAGS | SCO_VIEW_FEATURE_COALESCE | SCO_VIEW_FEATURE_HOST_PANELS));
    uint32_t tiny[2]{2, 0xdead};
    CHECK(view->capabilities(client, tiny) == SCO_OK && tiny[0] == 2 && tiny[1] != 0xdead);
    uint32_t bad[2]{1, 0};
    CHECK(view->capabilities(client, bad) == SCO_BAD_ARG && view->capabilities(client, nullptr) == SCO_BAD_ARG &&
          view->capabilities(nullptr, caps) == SCO_BAD_ARG);
    CHECK(view->stamps(nullptr, nullptr, nullptr, nullptr, nullptr) == SCO_BAD_ARG);
    CHECK(view->stamps(client, nullptr, nullptr, nullptr, nullptr) == SCO_OK); // every pointer may be NULL
}

static void TestRegistration() {
    const auto row = Mk(SCO_WIDGET_BUTTON, 1, 0, "Go");
    CHECK(SetTree(alpha, "alpha.reg", {row}) == SCO_OK);
    CHECK(view->menu_thread_enter(client) == SCO_WRONG_THREAD); // the game thread
    CHECK(view->menu_thread_enter(nullptr) == SCO_BAD_ARG && view->menu_thread_leave(nullptr) == SCO_BAD_ARG);
    {
        MenuThread a, b;
        uint32_t count = 8; sco_view_panel_entry entries[8]{}; uint32_t bytes = 0; uint64_t seq = 0;
        sco_view_event ev{}; ev.size = sizeof(ev); ev.node_id = 1; ev.kind = SCO_WIDGET_BUTTON; ev.row = -1; ev.epoch = 1; ev.node_key = 1;
        auto probe = [&](MenuThread& t, sco_result expect) {
            sco_result l = SCO_OK, c = SCO_OK, s = SCO_OK;
            count = 8; bytes = 0; entries[0].size = 0;
            t.Run([&] {
                l = view->list_panels(client, entries, sizeof(entries[0]), &count);
                c = view->copy_panel(client, "alpha.reg", nullptr, &bytes);
                s = view->submit_event(client, "alpha.reg", &ev, &seq);
            });
            return l == expect && c == expect && s == expect && entries[0].size == 0 && bytes == 0;
        };
        CHECK(probe(a, SCO_WRONG_THREAD)); // before any enter
        sco_result r = SCO_OK;
        a.Run([&] { r = view->menu_thread_enter(client); }); CHECK(r == SCO_OK);
        a.Run([&] { r = view->menu_thread_enter(client); }); CHECK(r == SCO_OK); // again from the same thread
        b.Run([&] { r = view->menu_thread_enter(client); }); CHECK(r == SCO_FAILED); // one at a time
        CHECK(probe(b, SCO_WRONG_THREAD));
        b.Run([&] { r = view->menu_thread_leave(client); }); CHECK(r == SCO_WRONG_THREAD); // changes nothing
        {
            sco_result l = SCO_FAILED;
            a.Run([&] { count = 8; l = view->list_panels(client, entries, sizeof(entries[0]), &count); });
            CHECK(l == SCO_OK && count >= 1);
        }
        CHECK(view->list_panels(client, entries, sizeof(entries[0]), &count) == SCO_WRONG_THREAD); // game thread
        a.Run([&] { r = view->menu_thread_leave(client); }); CHECK(r == SCO_OK);
        a.Run([&] { r = view->menu_thread_leave(client); }); CHECK(r == SCO_OK); // nothing registered
        CHECK(probe(a, SCO_WRONG_THREAD)); // after leave
        // A thread that never left: the host clears it after the join.
        b.Run([&] { r = view->menu_thread_enter(client); }); CHECK(r == SCO_OK);
        CHECK(V::ViewReleaseMenuThread());
        CHECK(!V::ViewReleaseMenuThread());
        a.Run([&] { r = view->menu_thread_enter(client); }); CHECK(r == SCO_OK);
        a.Run([&] { r = view->menu_thread_leave(client); }); CHECK(r == SCO_OK);
    }
    CHECK(table->remove_panel(alpha, "alpha.reg") == SCO_OK);
}

static std::vector<Spec> RichTree() {
    std::vector<Spec> t;
    t.push_back(Mk(SCO_WIDGET_COLUMN, 1, 0, "Root"));
    t.push_back(Mk(SCO_WIDGET_LABEL, 2, 1, "Heading"));
    t.back().text = "caption"; t.back().tooltip = "Tip"; t.back().severity = SCO_WIDGET_SEVERITY_WARNING;
    t.push_back(Mk(SCO_WIDGET_ROW, 3, 1, "Buttons"));
    t.push_back(Mk(SCO_WIDGET_BUTTON, 4, 3, "Apply", "rich.apply"));
    t.push_back(Mk(SCO_WIDGET_TOGGLE, 5, 3, "Enable"));
    t.back().value = 1; t.back().flags = SCO_WIDGET_DISABLED;
    t.push_back(Mk(SCO_WIDGET_SLIDER, 6, 1, "Level", "rich.level"));
    t.back().minimum = -5; t.back().maximum = 5; t.back().value = 2; t.back().presentation = SCO_WIDGET_NUMERIC_INTEGER;
    t.push_back(Mk(SCO_WIDGET_COMBO, 7, 1, "Choice"));
    t.back().items = {"One", "Two", "Th\xc3\xa9"}; t.back().index = 1; // a non-ASCII item
    t.push_back(Mk(SCO_WIDGET_LIST, 8, 1, "Rows"));
    t.back().items = {"a", "b", "c", "d", "e"}; t.back().index = -1; t.back().visibleRows = 2; t.back().firstRow = 2;
    t.push_back(Mk(SCO_WIDGET_TEXT_INPUT, 9, 1, "Name"));
    t.back().text = "synthetic name";
    t.push_back(Mk(SCO_WIDGET_TEXT_INPUT, 10, 1, "Secret", "rich.secret"));
    t.back().text = "synthetic secret"; t.back().flags = SCO_WIDGET_PASSWORD;
    t.push_back(Mk(SCO_WIDGET_TABLE, 11, 1, "Grid"));
    t.back().columns = 2; t.back().items = {"k1", "v1", "k2", "v2", "k3", "v3"}; t.back().visibleRows = 1;
    return t;
}

static void TestCopyAndList() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); }); CHECK(r == SCO_OK);
    const auto tree = RichTree();
    CHECK(SetTree(alpha, "alpha.rich", tree, nullptr, nullptr, "Rich panel") == SCO_OK);
    Blob b;
    CHECK(CopyOn(menu, "alpha.rich", b) == SCO_OK);
    CHECK(scv_validate(b.data(), b.bytes) == 0);
    const auto& h = b.H();
    CHECK(h.size == 96 && h.node_stride == 120 && h.node_count == tree.size() && h.total_bytes == b.bytes);
    CHECK(h.nodes_offset == 96 && h.items_offset == 96 + h.node_count * 120 && h.revision && h.epoch);
    CHECK(b.Str(h.panel_id) == "alpha.rich" && b.Str(h.title) == "Rich panel" && b.Str(h.owner) == "alpha");
    CHECK(h.flags == SCO_VIEW_PANEL_TAGGED_EVENTS);
    CHECK(h.item_total == 3 + 5 + 6);
    // Every node field, in order, against what the plugin submitted.
    for (uint32_t i = 0; i < tree.size(); ++i) {
        sco_view_node n{};
        CHECK(scv_node(b.data(), i, &n) == 1);
        const Spec& s = tree[i];
        CHECK(n.size == 120 && n.kind == s.kind && n.id == s.id && n.parent == s.parent && n.flags == s.flags);
        CHECK(n.index == s.index && n.severity == s.severity && n.first_row == s.firstRow);
        CHECK(n.visible_rows == s.visibleRows && n.presentation == s.presentation && n.columns == s.columns);
        CHECK(n.item_count == s.items.size() && n._reserved == 0);
        CHECK(n.value == s.value && n.minimum == s.minimum && n.maximum == s.maximum);
        CHECK(b.Str(n.label) == s.label && b.Str(n.tooltip) == s.tooltip && b.Str(n.tag) == s.tag);
        if (s.flags & SCO_WIDGET_PASSWORD) CHECK(n.text.length == 0 && b.Str(n.text).empty()); // never in a blob
        else CHECK(b.Str(n.text) == (s.kind == SCO_WIDGET_TEXT_INPUT ? s.text : std::string()));
        for (uint32_t k = 0; k < n.item_count; ++k) {
            sco_view_str it{};
            CHECK(scv_item(b.data(), n.first_item + k, &it) == 1 && b.Str(it) == s.items[k]);
        }
        CHECK(n.key == scv_reference_key(n.kind, n.id, n.parent, s.label.c_str(), s.label.size(), s.tag.c_str(), s.tag.size()));
    }
    // The secret appears nowhere in the bytes.
    const std::string raw(static_cast<const char*>(b.data()), b.bytes);
    CHECK(raw.find("synthetic secret") == std::string::npos && raw.find("synthetic name") != std::string::npos);

    // list_panels: order, counts, fields.
    const auto one = Mk(SCO_WIDGET_BUTTON, 1, 0, "Go");
    CHECK(SetTree(alpha, "alpha.second", {one}, nullptr, nullptr, "Second") == SCO_OK);
    sco_view_panel_entry list[4]{};
    uint32_t count = 4;
    menu.Run([&] { r = view->list_panels(client, list, sizeof(list[0]), &count); });
    CHECK(r == SCO_OK && count == 2);
    CHECK(std::strcmp(list[0].id, "alpha.rich") == 0 && std::strcmp(list[1].id, "alpha.second") == 0);
    CHECK(std::strcmp(list[0].title, "Rich panel") == 0 && std::strcmp(list[0].owner, "alpha") == 0);
    CHECK(list[0].size == 224 && list[0].node_count == tree.size() && list[1].node_count == 1);
    CHECK(list[0].flags == SCO_VIEW_PANEL_TAGGED_EVENTS && list[1].flags == 0 && list[0]._reserved == 0);
    CHECK(list[0].revision == h.revision && list[0].epoch == h.epoch);
    count = 1; sco_view_panel_entry tooSmall[1]{}; tooSmall[0].size = 77;
    menu.Run([&] { r = view->list_panels(client, tooSmall, sizeof(tooSmall[0]), &count); });
    CHECK(r == SCO_TOO_MANY && count == 2 && tooSmall[0].size == 77); // wrote nothing
    // A wider stride: each stride is zeroed and the tail is untouched by the reader.
    struct Wide { sco_view_panel_entry e; uint64_t tail[2]; } wide[2];
    std::memset(wide, 0xAB, sizeof(wide));
    count = 2;
    menu.Run([&] { r = view->list_panels(client, &wide[0].e, sizeof(Wide), &count); });
    CHECK(r == SCO_OK && count == 2 && wide[0].tail[0] == 0 && wide[1].tail[1] == 0 && wide[1].e.node_count == 1);
    uint32_t zero = 0;
    menu.Run([&] { r = view->list_panels(client, list, sizeof(list[0]) + 4, &zero); }); CHECK(r == SCO_BAD_ARG); // stride % 8
    menu.Run([&] { r = view->list_panels(client, list, sizeof(list[0]) - 8, &zero); }); CHECK(r == SCO_BAD_ARG); // stride too small
    menu.Run([&] { r = view->list_panels(client, list, sizeof(list[0]), nullptr); }); CHECK(r == SCO_BAD_ARG);
    alignas(8) unsigned char raw8[sizeof(sco_view_panel_entry) * 2 + 8]{};
    count = 2;
    menu.Run([&] { r = view->list_panels(client, reinterpret_cast<sco_view_panel_entry*>(raw8 + 4), sizeof(list[0]), &count); });
    CHECK(r == SCO_BAD_ARG); // misaligned
    // copy_panel: size handshake, nothing written when too small, alignment, ids.
    uint32_t need = 0;
    menu.Run([&] { r = view->copy_panel(client, "alpha.rich", nullptr, &need); });
    CHECK(r == SCO_TOO_MANY && need == b.bytes);
    std::vector<uint64_t> small(need / 8 + 2, 0x1111111111111111ull);
    uint32_t cap = need - 1;
    menu.Run([&] { r = view->copy_panel(client, "alpha.rich", small.data(), &cap); });
    CHECK(r == SCO_TOO_MANY && cap == need && small[0] == 0x1111111111111111ull);
    cap = need; std::vector<uint64_t> exact((need + 7) / 8, 0);
    menu.Run([&] { r = view->copy_panel(client, "alpha.rich", exact.data(), &cap); });
    CHECK(r == SCO_OK && cap == need && std::memcmp(exact.data(), b.data(), need) == 0);
    cap = need;
    menu.Run([&] { r = view->copy_panel(client, "alpha.rich", reinterpret_cast<unsigned char*>(exact.data()) + 4, &cap); });
    CHECK(r == SCO_BAD_ARG);
    cap = need;
    menu.Run([&] { r = view->copy_panel(client, "alpha.rich", nullptr, &cap); }); // capacity but no buffer
    CHECK(r == SCO_BAD_ARG);
    menu.Run([&] { r = view->copy_panel(client, "alpha.absent", exact.data(), &cap); }); CHECK(r == SCO_NOT_FOUND);
    menu.Run([&] { r = view->copy_panel(client, nullptr, exact.data(), &cap); }); CHECK(r == SCO_BAD_ARG);
    menu.Run([&] { r = view->copy_panel(client, std::string(64, 'x').c_str(), exact.data(), &cap); }); CHECK(r == SCO_BAD_ARG);
    menu.Run([&] { r = view->copy_panel(client, "alpha.rich", exact.data(), nullptr); }); CHECK(r == SCO_BAD_ARG);

    // A panel that grows between the size answer and the next call: the caller loops.
    const uint64_t builds = W::SnapshotBuilds();
    uint32_t first = 0;
    menu.Run([&] { r = view->copy_panel(client, "alpha.second", nullptr, &first); });
    CHECK(r == SCO_TOO_MANY);
    auto grown = std::vector<Spec>{one, Mk(SCO_WIDGET_LABEL, 2, 0, std::string(200, 'g'))};
    CHECK(SetTree(alpha, "alpha.second", grown, nullptr, nullptr, "Second") == SCO_OK);
    std::vector<uint64_t> buffer((first + 7) / 8, 0);
    cap = first;
    menu.Run([&] { r = view->copy_panel(client, "alpha.second", buffer.data(), &cap); });
    CHECK(r == SCO_TOO_MANY && cap > first);
    Blob g;
    CHECK(CopyOn(menu, "alpha.second", g) == SCO_OK && scv_validate(g.data(), g.bytes) == 0 && g.H().node_count == 2);
    // Repeat copies of an unchanged panel reuse one shared snapshot (SnapshotBuilds counts builds).
    const uint64_t afterFirst = W::SnapshotBuilds();
    Blob g2; CHECK(CopyOn(menu, "alpha.second", g2) == SCO_OK);
    CHECK(W::SnapshotBuilds() == afterFirst && afterFirst >= builds && g2.H().revision == g.H().revision);

    // Limits: 256 nodes, 255-byte strings, 256 items.
    std::vector<Spec> wide256;
    for (uint32_t i = 1; i <= 255; ++i) wide256.push_back(Mk(SCO_WIDGET_LABEL, i, 0, std::string(255, 'x')));
    wide256.push_back(Mk(SCO_WIDGET_LIST, 256, 0, "Many"));
    for (uint32_t i = 0; i < 256; ++i) wide256.back().items.push_back(std::string(255, 'y'));
    wide256.back().index = -1;
    CHECK(SetTree(alpha, "alpha.big", wide256) == SCO_OK);
    Blob big;
    CHECK(CopyOn(menu, "alpha.big", big) == SCO_OK && scv_validate(big.data(), big.bytes) == 0);
    CHECK(big.H().node_count == 256 && big.H().item_total == 256);
    sco_view_node last{};
    CHECK(big.Node(256, last) && last.item_count == 256);
    sco_view_str lastItem{};
    CHECK(scv_item(big.data(), last.first_item + 255, &lastItem) == 1 && lastItem.length == 255);
    CHECK(table->remove_panel(alpha, "alpha.big") == SCO_OK);

    // A host panel is flagged and its owner is "menu".
    sco_plugin* menuOwner = sco::host::NewMenuOwner();
    CHECK(menuOwner != nullptr);
    if (menuOwner) {
        CHECK(table->set_panel(menuOwner, "menu.main", "Menu", &Build({Mk(SCO_WIDGET_LABEL, 1, 0, "Hi")}, 0, 1).nodes[0].base.base.base, 1,
                               sizeof(sco_widget_node_1_3), nullptr, nullptr) == SCO_OK);
        Blob m;
        CHECK(CopyOn(menu, "menu.main", m) == SCO_OK && m.H().flags == SCO_VIEW_PANEL_HOST_OWNED && m.Str(m.H().owner) == "menu");
        sco_view_panel_entry rows[8]{}; count = 8;
        menu.Run([&] { r = view->list_panels(client, rows, sizeof(rows[0]), &count); });
        bool found = false;
        for (uint32_t i = 0; i < count; ++i)
            if (std::strcmp(rows[i].id, "menu.main") == 0) found = rows[i].flags == SCO_VIEW_PANEL_HOST_OWNED && std::strcmp(rows[i].owner, "menu") == 0;
        CHECK(found);
        CHECK(table->remove_panel(menuOwner, "menu.main") == SCO_OK);
    }
    RemoveAll();
    menu.Run([&] { r = view->menu_thread_leave(client); }); CHECK(r == SCO_OK);
}

// The golden vectors: the key function is invented for this interface and pinned here.
static void TestKeyVectors() {
    struct Vector { uint32_t kind, id, parent; std::string label, tag; uint64_t key; };
    const std::vector<Vector> vectors{
        {SCO_WIDGET_BUTTON, 1, 0, "Apply", "apply", 0x9589264ec1ef83f7ull},
        {SCO_WIDGET_TOGGLE, 1, 0, "Apply", "apply", 0x053d75458a0cc374ull},
        {SCO_WIDGET_SLIDER, 9, 4, "ignored label", std::string(63, 'a'), 0x0ed3b2adb02f9c8cull},
        {SCO_WIDGET_BUTTON, 1, 0, "Apply", "", 0xbb00ed9470146ec5ull},
        {SCO_WIDGET_LABEL, 5, 3, "", "", 0xda6f7648443f0b17ull},
        {SCO_WIDGET_BUTTON, 7, 0, std::string(255, 'x'), "", 0x90f04a2e25cd996full},
        {SCO_WIDGET_BUTTON, 1, 0, "apply", "", 0x8014cca4ac6d3025ull},
    };
    for (const auto& v : vectors) {
        W::Node n;
        n.kind = v.kind; n.id = v.id; n.parent = v.parent; n.label = v.label; n.tag = v.tag;
        CHECK(V::NodeKey(n) == v.key);
        CHECK(scv_reference_key(v.kind, v.id, v.parent, v.label.c_str(), v.label.size(), v.tag.c_str(), v.tag.size()) == v.key);
    }
    // A tagged key ignores id, parent and label; an untagged key covers them.
    W::Node a, b;
    a.kind = b.kind = SCO_WIDGET_BUTTON; a.tag = b.tag = "t"; a.id = 1; b.id = 2; a.parent = 0; b.parent = 7; a.label = "x"; b.label = "y";
    CHECK(V::NodeKey(a) == V::NodeKey(b));
    a.tag = b.tag = ""; CHECK(V::NodeKey(a) != V::NodeKey(b));
    b = a; b.parent = 1; CHECK(V::NodeKey(a) != V::NodeKey(b));
    b = a; b.id = 3; CHECK(V::NodeKey(a) != V::NodeKey(b));
    b = a; b.label = "other"; CHECK(V::NodeKey(a) != V::NodeKey(b));
    b = a; b.kind = SCO_WIDGET_TOGGLE; CHECK(V::NodeKey(a) != V::NodeKey(b));
    CHECK(V::NodeKey(a) != 0);
}

static void TestRevisions() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    std::vector<Spec> t{Mk(SCO_WIDGET_SLIDER, 1, 0, "Level", "rev.level"), Mk(SCO_WIDGET_BUTTON, 2, 0, "Go", "rev.go"), Mk(SCO_WIDGET_LABEL, 3, 0, "Text")};
    t[0].maximum = 10;
    const Stamps s0 = Read();
    CHECK(SetTree(alpha, "alpha.rev", t, OnEvent, &rec) == SCO_OK);
    Blob b1; CHECK(CopyOn(menu, "alpha.rev", b1) == SCO_OK);
    const Stamps s1 = Read();
    CHECK(b1.H().revision > s0.global && b1.H().epoch > 0 && s1.global >= b1.H().revision && s1.list > s0.list); // a new panel is listed
    // update_nodes: revision moves, epoch does not; the node count did not change, so the list did not move.
    t[2].label = "Text 2";
    CHECK(UpdateTree(alpha, "alpha.rev", t, 2, 1) == SCO_OK);
    Blob b2; CHECK(CopyOn(menu, "alpha.rev", b2) == SCO_OK);
    const Stamps s2 = Read();
    CHECK(b2.H().revision > b1.H().revision && b2.H().epoch == b1.H().epoch && s2.global > s1.global && s2.list == s1.list);
    // remove_nodes: the node count changes, so the list moves.
    const uint32_t gone = 3;
    CHECK(table->remove_nodes(alpha, "alpha.rev", &gone, 1) == SCO_OK);
    Blob b3; CHECK(CopyOn(menu, "alpha.rev", b3) == SCO_OK);
    const Stamps s3 = Read();
    CHECK(b3.H().revision > b2.H().revision && b3.H().epoch == b1.H().epoch && b3.H().node_count == 2 && s3.list > s2.list);
    // An event that changes a value moves the revision and not the epoch; a BUTTON changes nothing.
    sco_view_event e = EventFor(b3, 1, SCO_WIDGET_SLIDER); e.value = 4;
    uint64_t seq = 0;
    CHECK(SubmitOn(menu, "alpha.rev", e, &seq) == SCO_OK);
    CHECK(Read().global == s3.global); // queued only: nothing changed yet
    Drain d = DoDrain();
    CHECK(d.result == SCO_OK && d.dispatched == 1 && d.dropped == 0);
    Blob b4; CHECK(CopyOn(menu, "alpha.rev", b4) == SCO_OK);
    CHECK(b4.H().revision > b3.H().revision && b4.H().epoch == b3.H().epoch);
    sco_view_node n{}; CHECK(b4.Node(1, n) && n.value == 4);
    const Stamps s4 = Read();
    sco_view_event press = EventFor(b4, 2, SCO_WIDGET_BUTTON);
    CHECK(SubmitOn(menu, "alpha.rev", press, &seq) == SCO_OK);
    d = DoDrain();
    CHECK(d.dispatched == 1 && rec.got.size() == 2);
    Blob b5; CHECK(CopyOn(menu, "alpha.rev", b5) == SCO_OK);
    CHECK(b5.H().revision == b4.H().revision && Read().global == s4.global);
    // set_panel moves both; remove and add again never reuses a value.
    CHECK(SetTree(alpha, "alpha.rev", t, OnEvent, &rec) == SCO_OK);
    Blob b6; CHECK(CopyOn(menu, "alpha.rev", b6) == SCO_OK);
    CHECK(b6.H().epoch > b5.H().epoch && b6.H().revision > b5.H().revision);
    CHECK(table->remove_panel(alpha, "alpha.rev") == SCO_OK);
    const Stamps s7 = Read();
    CHECK(s7.list > s4.list);
    CHECK(SetTree(alpha, "alpha.rev", t, OnEvent, &rec) == SCO_OK);
    Blob b7; CHECK(CopyOn(menu, "alpha.rev", b7) == SCO_OK);
    CHECK(b7.H().epoch > b6.H().epoch && b7.H().revision > b6.H().revision);
    CHECK(Read().list > s7.list);
    // The title is listed state: a replace that changes it moves list_revision, an identical one does not.
    const Stamps s8 = Read();
    CHECK(SetTree(alpha, "alpha.rev", t, OnEvent, &rec, "Renamed") == SCO_OK);
    const Stamps s9 = Read();
    CHECK(s9.list > s8.list);
    CHECK(SetTree(alpha, "alpha.rev", t, OnEvent, &rec, "Renamed") == SCO_OK);
    CHECK(Read().list == s9.list && Read().global > s9.global);
    CHECK(table->remove_panel(alpha, "alpha.rev") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// ---- the republish and key-matching table (2.3.1) ----
struct Scenario {
    const char* name;
    // outcome for a tagged and an untagged control: delivered?, and the id the callback sees
    bool taggedDelivered, untaggedDelivered;
    uint32_t taggedId, untaggedId;
};
enum Mutation { kNone, kUpdateOther, kUpdateRelabel, kUpdateReplace, kSetSame, kSetRenumber, kSetRelabel, kSetRemoved,
                kSetDisabled, kSetDifferent, kUpdateValue, kMutationCount };
static const Scenario kScenarios[kMutationCount] = {
    {"nothing",                         true,  true,  10, 10},
    {"update_nodes of other nodes",     true,  true,  10, 10},
    {"update_nodes relabels (equal epoch)", true, false, 10, 10},
    {"update_nodes replaces the subtree", false, false, 10, 10},
    {"set_panel same tree",             true,  true,  10, 10},
    {"set_panel ids renumbered",        true,  false, 20, 10},
    {"set_panel relabel",               true,  false, 10, 10},
    {"set_panel removes the control",   false, false, 10, 10},
    {"set_panel disables the control",  false, false, 10, 10},
    {"set_panel different control under the old id", false, false, 10, 10},
    {"update_nodes new value, still enabled", true, true, 10, 10},
};

static std::vector<Spec> ScenarioTree(uint32_t kind, bool tagged) {
    std::vector<Spec> t;
    t.push_back(Mk(SCO_WIDGET_ROW, 1, 0, "Row"));
    uint32_t nodeKind = kind == SCO_WIDGET_EVENT_PAGE ? SCO_WIDGET_LIST : kind;
    Spec c = Mk(nodeKind, 10, 1, "Control", tagged ? "ctl.main" : "");
    if (nodeKind == SCO_WIDGET_SLIDER) { c.minimum = 0; c.maximum = 10; c.value = 2; }
    if (nodeKind == SCO_WIDGET_COMBO || nodeKind == SCO_WIDGET_LIST) { c.items = {"A", "B", "C", "D", "E", "F"}; c.index = 0; }
    if (kind == SCO_WIDGET_EVENT_PAGE) c.visibleRows = 2;
    if (nodeKind == SCO_WIDGET_TEXT_INPUT) c.text = "old";
    t.push_back(c);
    t.push_back(Mk(SCO_WIDGET_BUTTON, 11, 1, "Other", tagged ? "other.btn" : ""));
    return t;
}
static sco_view_event ScenarioEvent(const Blob& b, uint32_t kind) {
    sco_view_event e = EventFor(b, 10, kind);
    if (kind == SCO_WIDGET_TOGGLE) e.value = 1;
    if (kind == SCO_WIDGET_SLIDER) e.value = 5;
    if (kind == SCO_WIDGET_COMBO || kind == SCO_WIDGET_LIST) e.index = 1;
    if (kind == SCO_WIDGET_EVENT_PAGE) e.row = 2;
    return e;
}
static void ApplyMutation(Mutation m, std::vector<Spec> t, bool tagged, Rec& rec) {
    auto set = [&](const std::vector<Spec>& tree) { CHECK(SetTree(alpha, "alpha.rep", tree, OnEvent, &rec) == SCO_OK); };
    switch (m) {
        case kNone: break;
        case kUpdateOther: t[2].label = "Other 2"; CHECK(UpdateTree(alpha, "alpha.rep", t, 2, 1) == SCO_OK); break;
        case kUpdateRelabel: t[1].label = "Renamed"; CHECK(UpdateTree(alpha, "alpha.rep", t, 1, 1) == SCO_OK); break;
        case kUpdateReplace:
            t[1].label = "Different"; if (tagged) t[1].tag = "different.tag";
            CHECK(UpdateTree(alpha, "alpha.rep", t, 0, 3) == SCO_OK); break;
        case kSetSame: t[2].label = "Other 3"; set(t); break;
        case kSetRenumber: t[0].id = 2; t[1].id = 20; t[1].parent = 2; t[2].id = 21; t[2].parent = 2; set(t); break;
        case kSetRelabel: t[1].label = "Renamed"; set(t); break;
        case kSetRemoved: t.erase(t.begin() + 1); set(t); break;
        case kSetDisabled: t[1].flags = SCO_WIDGET_DISABLED; set(t); break;
        case kSetDifferent:
            t[1].kind = t[1].kind == SCO_WIDGET_BUTTON ? SCO_WIDGET_TOGGLE : SCO_WIDGET_BUTTON;
            t[1].items.clear(); t[1].index = 0; t[1].minimum = t[1].maximum = t[1].value = 0; t[1].text.clear();
            t[1].visibleRows = 0; t[1].label = "Delete"; if (tagged) t[1].tag = "delete.btn";
            set(t); break;
        case kUpdateValue:
            if (t[1].kind == SCO_WIDGET_SLIDER) t[1].value = 3;
            if (t[1].kind == SCO_WIDGET_TOGGLE) t[1].value = 1;
            if (t[1].kind == SCO_WIDGET_TEXT_INPUT) t[1].text = "changed";
            if (t[1].kind == SCO_WIDGET_COMBO || t[1].kind == SCO_WIDGET_LIST) t[1].index = 2;
            CHECK(UpdateTree(alpha, "alpha.rep", t, 1, 1) == SCO_OK); break;
        default: break;
    }
}
static void TestRepublishTable() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    const uint32_t kinds[] = {SCO_WIDGET_BUTTON, SCO_WIDGET_TOGGLE, SCO_WIDGET_SLIDER, SCO_WIDGET_COMBO,
                              SCO_WIDGET_LIST, SCO_WIDGET_EVENT_PAGE, SCO_WIDGET_TEXT_INPUT};
    for (const uint32_t kind : kinds) for (const bool tagged : {true, false}) for (int mi = 0; mi < kMutationCount; ++mi) {
        const Mutation m = static_cast<Mutation>(mi);
        const Scenario& sc = kScenarios[mi];
        Rec rec;
        const auto tree = ScenarioTree(kind, tagged);
        CHECK(SetTree(alpha, "alpha.rep", tree, OnEvent, &rec) == SCO_OK);
        Blob b; CHECK(CopyOn(menu, "alpha.rep", b) == SCO_OK);
        sco_view_event e = ScenarioEvent(b, kind);
        if (kind == SCO_WIDGET_TEXT_INPUT) e.text = "new";
        uint64_t seq = 0;
        CHECK(SubmitOn(menu, "alpha.rep", e, &seq) == SCO_OK);
        const Stamps before = Read();
        ApplyMutation(m, tree, tagged, rec);
        const Drain d = DoDrain();
        const Stamps after = Read();
        const bool expect = tagged ? sc.taggedDelivered : sc.untaggedDelivered;
        const uint32_t expectId = tagged ? sc.taggedId : sc.untaggedId;
        const bool delivered = expect;
        if (!(d.result == SCO_OK && rec.got.size() == (delivered ? 1u : 0u) && d.dispatched == (delivered ? 1u : 0u) &&
              d.dropped == (delivered ? 0u : 1u) && after.drops - before.drops == (delivered ? 0u : 1u) && after.done == seq))
            std::printf("  scenario kind=%u tagged=%d: %s\n", kind, tagged, sc.name);
        CHECK(d.result == SCO_OK);
        CHECK(rec.got.size() == (delivered ? 1u : 0u));
        CHECK(d.dispatched == (delivered ? 1u : 0u) && d.dropped == (delivered ? 0u : 1u));
        CHECK(after.drops - before.drops == (delivered ? 0u : 1u));
        CHECK(after.done == seq); // finished either way
        if (delivered && rec.got.size() == 1) {
            const Got& g = rec.got[0];
            CHECK(g.id == expectId && g.kind == kind);
            if (tagged) CHECK(g.tag == "ctl.main" && g.size == sizeof(sco_widget_event_1_4)); // the live tag, not the menu's
            W::Node live;
            CHECK(LiveNode("alpha.rep", expectId, live));
            if (kind == SCO_WIDGET_TOGGLE) CHECK(live.value == 1);
            if (kind == SCO_WIDGET_SLIDER) CHECK(live.value == 5);
            if (kind == SCO_WIDGET_COMBO || kind == SCO_WIDGET_LIST) CHECK(live.index == 1);
            if (kind == SCO_WIDGET_EVENT_PAGE) CHECK(live.first_row == 2 && g.row == 2);
            if (kind == SCO_WIDGET_TEXT_INPUT) CHECK(live.text == "new");
            // The other control was never touched.
            W::Node other; CHECK(LiveNode("alpha.rep", expectId == 20 ? 21 : 11, other) && other.kind == SCO_WIDGET_BUTTON);
        }
        // Exactly once: nothing is left to run.
        const Drain again = DoDrain();
        CHECK(again.dispatched == 0 && again.dropped == 0 && rec.got.size() == (delivered ? 1u : 0u));
        CHECK(table->remove_panel(alpha, "alpha.rep") == SCO_OK);
    }
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// The same candidates through RenderPanel and through the view path must agree.
struct HeadlessRenderer : W::Renderer {
    std::vector<W::Event> candidates;
    void Render(const W::Panel&, std::vector<W::Event>& events) override { events = candidates; }
};
static void TestParityWithRenderPanel() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    std::vector<Spec> t;
    t.push_back(Mk(SCO_WIDGET_BUTTON, 2, 0, "Btn"));
    t.push_back(Mk(SCO_WIDGET_TOGGLE, 3, 0, "Tog"));
    t.push_back(Mk(SCO_WIDGET_SLIDER, 4, 0, "Sld")); t.back().maximum = 4;
    t.push_back(Mk(SCO_WIDGET_COMBO, 5, 0, "Cmb")); t.back().items = {"A", "B"};
    t.push_back(Mk(SCO_WIDGET_LIST, 6, 0, "Lst")); t.back().items = {"A", "B", "C"}; t.back().visibleRows = 2;
    t.push_back(Mk(SCO_WIDGET_TEXT_INPUT, 7, 0, "Txt")); t.back().text = "Old";
    t.push_back(Mk(SCO_WIDGET_LABEL, 8, 0, "Lab"));
    t.push_back(Mk(SCO_WIDGET_BUTTON, 9, 0, "Off")); t.back().flags = SCO_WIDGET_DISABLED;
    t.push_back(Mk(SCO_WIDGET_SLIDER, 12, 0, "Int")); t.back().maximum = 9; t.back().presentation = SCO_WIDGET_NUMERIC_INTEGER;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<W::Event> cases{
        {2, 2}, {3, 3, 0, 1}, {3, 3, 0, 2}, {3, 3, 1, 1}, {4, 4, 0, 3}, {4, 4, 0, 5}, {4, 4, 0, -1}, {5, 5, 1}, {5, 5, 2}, {5, 5, -1},
        {5, 5, -2}, {6, 6, 1}, {6, 6, 3}, {7, 7, 0, 0, "New"}, {7, 7, 0, 0, std::string(255, 'x')}, {7, 7, 0, 0, std::string(256, 'x')},
        {7, 7, 0, 0, "\xff"}, {7, 7, 1, 0, "x"}, {7, 7, 0, 1, "x"}, {2, 3}, {3, 2}, {8, 8}, {8, 2}, {9, 2}, {999, 2}, {2, 2, 1}, {2, 2, 0, 1},
        {2, 2, 0, 0, "stray"}, {3, 3, 0, nan}, {12, 4, 0, 2.5}, {12, 4, 0, 3},
        {6, SCO_WIDGET_EVENT_PAGE, 0, 0, "", 1}, {6, SCO_WIDGET_EVENT_PAGE, 0, 0, "", 3}, {6, SCO_WIDGET_EVENT_PAGE, 1, 0, "", 1},
        {5, SCO_WIDGET_EVENT_PAGE, 0, 0, "", 0}, {6, SCO_WIDGET_EVENT_PAGE, 0, 0, "", -1}};
    int disagreements = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        const W::Event& c = cases[i];
        Rec viaRender, viaView;
        CHECK(SetTree(alpha, "alpha.par", t, OnEvent, &viaRender) == SCO_OK);
        HeadlessRenderer renderer; renderer.candidates = {c};
        CHECK(W::RenderPanel("alpha.par", renderer) == Result::Ok);
        const W::Node afterRender = [&] { W::Node n; LiveNode("alpha.par", c.node_id, n); return n; }();
        CHECK(table->remove_panel(alpha, "alpha.par") == SCO_OK);
        CHECK(SetTree(alpha, "alpha.par", t, OnEvent, &viaView) == SCO_OK);
        Blob b; CHECK(CopyOn(menu, "alpha.par", b) == SCO_OK);
        sco_view_event e = EventFor(b, c.node_id, c.kind);
        sco_view_node n{};
        if (!b.Node(c.node_id, n)) { // an id that does not exist: the key of a plausible control
            e.node_key = scv_reference_key(c.kind, c.node_id, 0, "x", 1, "", 0);
        }
        e.index = c.index; e.value = c.value; e.row = c.row; e.text = c.kind == SCO_WIDGET_TEXT_INPUT || !c.text.empty() ? c.text.c_str() : nullptr;
        uint64_t seq = 0;
        const sco_result submitted = SubmitOn(menu, "alpha.par", e, &seq);
        if (submitted == SCO_OK) DoDrain();
        // Refused at the shape check, or dropped at dispatch, or delivered: the callback is the outcome.
        const bool a = !viaRender.got.empty(), bv = !viaView.got.empty();
        if (a != bv) { ++disagreements; std::printf("  parity case %zu differs: render=%d view=%d\n", i, a, bv); }
        if (a && bv) {
            CHECK(viaRender.got[0].index == viaView.got[0].index && viaRender.got[0].value == viaView.got[0].value &&
                  viaRender.got[0].text == viaView.got[0].text && viaRender.got[0].row == viaView.got[0].row);
            W::Node afterView; LiveNode("alpha.par", c.node_id, afterView);
            CHECK(afterView.value == afterRender.value && afterView.index == afterRender.index &&
                  afterView.text == afterRender.text && afterView.first_row == afterRender.first_row);
        }
        CHECK(table->remove_panel(alpha, "alpha.par") == SCO_OK);
    }
    CHECK(disagreements == 0);

    // The one deliberate difference: no generation break inside one drain. A callback that calls
    // update_nodes does not drop the later slots; RenderPanel drops them.
    std::vector<Spec> two{Mk(SCO_WIDGET_BUTTON, 1, 0, "First", "first"), Mk(SCO_WIDGET_BUTTON, 2, 0, "Second", "second")};
    Rec renderRec, viewRec;
    auto relabel = [&](const Got&) { two[1].label = "Second again"; CHECK(UpdateTree(alpha, "alpha.par", two, 1, 1) == SCO_OK); };
    renderRec.action = relabel;
    CHECK(SetTree(alpha, "alpha.par", two, OnEvent, &renderRec) == SCO_OK);
    HeadlessRenderer renderer; renderer.candidates = {{1, SCO_WIDGET_BUTTON}, {2, SCO_WIDGET_BUTTON}};
    CHECK(W::RenderPanel("alpha.par", renderer) == Result::Ok);
    CHECK(renderRec.got.size() == 1); // RenderPanel: the update_nodes ended the pass
    CHECK(table->remove_panel(alpha, "alpha.par") == SCO_OK);
    two[1].label = "Second";
    viewRec.action = relabel;
    CHECK(SetTree(alpha, "alpha.par", two, OnEvent, &viewRec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.par", b) == SCO_OK);
    uint64_t s1 = 0, s2 = 0;
    CHECK(SubmitOn(menu, "alpha.par", EventFor(b, 1, SCO_WIDGET_BUTTON), &s1) == SCO_OK);
    CHECK(SubmitOn(menu, "alpha.par", EventFor(b, 2, SCO_WIDGET_BUTTON), &s2) == SCO_OK);
    const Drain d = DoDrain();
    CHECK(d.dispatched == 2 && d.dropped == 0 && viewRec.got.size() == 2 && viewRec.got[1].id == 2); // tagged: still delivered
    CHECK(table->remove_panel(alpha, "alpha.par") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// ---- submit shape, coalescing, events_done (2.3.2) ----
static void TestSubmitShape() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    std::vector<Spec> t{Mk(SCO_WIDGET_BUTTON, 1, 0, "Go"), Mk(SCO_WIDGET_TEXT_INPUT, 2, 0, "Name")};
    CHECK(SetTree(alpha, "alpha.shape", t, OnEvent, &rec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.shape", b) == SCO_OK);
    const sco_view_event good = EventFor(b, 1, SCO_WIDGET_BUTTON);
    uint64_t seq = 99;
    auto submit = [&](sco_view_event e, const char* id = "alpha.shape") { seq = 99; return SubmitOn(menu, id, e, &seq); };
    auto with = [&](auto&& edit) { sco_view_event e = good; edit(e); return e; };
    CHECK(submit(with([](auto& e) { e.size = sizeof(e) - 1; })) == SCO_BAD_ARG && seq == 99);
    CHECK(submit(with([](auto& e) { e.kind = SCO_WIDGET_LABEL; })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.kind = SCO_WIDGET_TABLE; })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.kind = SCO_WIDGET_ROW; })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.kind = 0; })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.kind = 99; })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.value = std::numeric_limits<double>::infinity(); })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.value = std::numeric_limits<double>::quiet_NaN(); })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e._reserved = 1; })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.epoch = 0; })) == SCO_BAD_ARG);
    CHECK(submit(with([](auto& e) { e.node_key = 0; })) == SCO_BAD_ARG);
    const std::string longText(256, 'x'), maxText(255, 'x');
    CHECK(submit(with([&](auto& e) { e.kind = SCO_WIDGET_TEXT_INPUT; e.text = longText.c_str(); })) == SCO_BAD_ARG);
    CHECK(submit(with([&](auto& e) { e.kind = SCO_WIDGET_TEXT_INPUT; e.text = "\xff"; })) == SCO_BAD_ARG);
    CHECK(submit(with([&](auto& e) { e.kind = SCO_WIDGET_TEXT_INPUT; e.text = "\xed\xa0\x80"; })) == SCO_BAD_ARG);
    CHECK(submit(good, "alpha.absent") == SCO_NOT_FOUND && seq == 99);
    CHECK(submit(good, "alpha.shape") == SCO_OK);
    const uint64_t first = seq;
    const auto textEvent = [&](const char* text) { auto e = EventFor(b, 2, SCO_WIDGET_TEXT_INPUT); e.text = text; return e; };
    CHECK(submit(textEvent(maxText.c_str())) == SCO_OK && seq == first + 1);
    CHECK(submit(textEvent(nullptr)) == SCO_OK && seq == first + 2); // NULL = empty; merges into the slot above
    CHECK(SubmitOn(menu, "alpha.shape", good, nullptr) == SCO_OK); // seq pointer may be NULL
    CHECK(SubmitOn(menu, nullptr, good, &seq) == SCO_BAD_ARG);
    { sco_result s = SCO_OK; menu.Run([&] { s = view->submit_event(client, "alpha.shape", nullptr, &seq); }); CHECK(s == SCO_BAD_ARG); }
    { sco_result s = SCO_OK; menu.Run([&] { s = view->submit_event(nullptr, "alpha.shape", &good, &seq); }); CHECK(s == SCO_BAD_ARG); }
    const Drain d = DoDrain();
    CHECK(d.result == SCO_OK && d.dispatched == 3 && d.dropped == 0);
    CHECK(rec.got.size() == 3 && rec.got[1].text.empty()); // BUTTON, TEXT (255 then empty, merged), BUTTON
    CHECK(table->remove_panel(alpha, "alpha.shape") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

static std::vector<Spec> SliderTree(bool tagged) {
    std::vector<Spec> t;
    t.push_back(Mk(SCO_WIDGET_BUTTON, 1, 0, "A", tagged ? "btn.a" : ""));
    t.push_back(Mk(SCO_WIDGET_SLIDER, 2, 0, "S", tagged ? "sld.s" : "")); t.back().maximum = 1;
    t.push_back(Mk(SCO_WIDGET_BUTTON, 3, 0, "X", tagged ? "btn.x" : ""));
    t.push_back(Mk(SCO_WIDGET_TEXT_INPUT, 4, 0, "T", tagged ? "txt.t" : ""));
    return t;
}
static void TestCoalescing() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    auto tree = SliderTree(false);
    CHECK(SetTree(alpha, "alpha.co", tree, OnEvent, &rec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.co", b) == SCO_OK);
    const Stamps base = Read();
    auto slider = [&](double v) { auto e = EventFor(b, 2, SCO_WIDGET_SLIDER); e.value = v; return e; };
    uint64_t seq = 0;
    // The step-by-step table of the design page.
    CHECK(SubmitOn(menu, "alpha.co", EventFor(b, 1, SCO_WIDGET_BUTTON), &seq) == SCO_OK);
    const uint64_t s0 = seq - 1; // everything before has finished
    CHECK(s0 == base.done);
    CHECK(SubmitOn(menu, "alpha.co", slider(0.3), &seq) == SCO_OK && seq == s0 + 2);
    CHECK(SubmitOn(menu, "alpha.co", slider(0.5), &seq) == SCO_OK && seq == s0 + 3);
    CHECK(Read().done == s0); // nothing is finished before the pass
    Drain d = DoDrain();
    CHECK(d.result == SCO_OK && d.dispatched == 2 && d.dropped == 0); // two slots handled
    CHECK(Read().done == s0 + 3 && rec.got.size() == 2 && rec.got[1].value == 0.5); // the newest value only
    // Not adjacent: SLIDER, BUTTON, SLIDER are three slots.
    rec.got.clear();
    CHECK(SubmitOn(menu, "alpha.co", slider(0.3), &seq) == SCO_OK);
    CHECK(SubmitOn(menu, "alpha.co", EventFor(b, 3, SCO_WIDGET_BUTTON), &seq) == SCO_OK);
    CHECK(SubmitOn(menu, "alpha.co", slider(0.6), &seq) == SCO_OK);
    const uint64_t lastSeq = seq;
    d = DoDrain();
    CHECK(d.dispatched == 3 && rec.got.size() == 3 && rec.got[0].value == 0.3 && rec.got[1].id == 3 && rec.got[2].value == 0.6);
    CHECK(Read().done == lastSeq);
    // Different epoch: two slots, matched separately.
    Blob b2;
    tree[0].label = "A2"; CHECK(SetTree(alpha, "alpha.co", tree, OnEvent, &rec) == SCO_OK); // untagged: new epoch, same slider
    CHECK(CopyOn(menu, "alpha.co", b2) == SCO_OK && b2.H().epoch != b.H().epoch);
    rec.got.clear();
    auto e1 = slider(0.2); // epoch E
    auto e2 = EventFor(b2, 2, SCO_WIDGET_SLIDER); e2.value = 0.4; // epoch E+1, same key (same id, parent, label)
    CHECK(e1.node_key == e2.node_key && e1.epoch != e2.epoch);
    uint64_t q1 = 0, q2 = 0;
    CHECK(SubmitOn(menu, "alpha.co", e1, &q1) == SCO_OK && SubmitOn(menu, "alpha.co", e2, &q2) == SCO_OK && q2 == q1 + 1);
    d = DoDrain();
    CHECK(d.dispatched == 2 && rec.got.size() == 2 && rec.got[0].value == 0.2 && rec.got[1].value == 0.4 && Read().done == q2);
    // Different key, same node id and epoch: untagged slider relabelled by update_nodes between two copies.
    const Stamps pre = Read();
    rec.got.clear();
    auto k1 = EventFor(b2, 2, SCO_WIDGET_SLIDER); k1.value = 0.1;
    tree[1].label = "S relabelled";
    CHECK(UpdateTree(alpha, "alpha.co", tree, 1, 1) == SCO_OK);
    Blob b3; CHECK(CopyOn(menu, "alpha.co", b3) == SCO_OK && b3.H().epoch == b2.H().epoch);
    auto k2 = EventFor(b3, 2, SCO_WIDGET_SLIDER); k2.value = 0.9;
    CHECK(k1.node_key != k2.node_key && k1.epoch == k2.epoch);
    CHECK(SubmitOn(menu, "alpha.co", k1, &q1) == SCO_OK && SubmitOn(menu, "alpha.co", k2, &q2) == SCO_OK && q2 == q1 + 1);
    d = DoDrain();
    CHECK(d.dispatched == 1 && d.dropped == 1 && rec.got.size() == 1 && rec.got[0].value == 0.9); // slot 1 matched with K1: dropped
    CHECK(Read().done == q2 && Read().drops == pre.drops + 1);
    // Same key, same node id and epoch, tail slot: one slot that keeps its key (the text merges too).
    rec.got.clear();
    auto t1 = EventFor(b3, 4, SCO_WIDGET_TEXT_INPUT); t1.text = "one";
    auto t2 = t1; t2.text = "two";
    CHECK(SubmitOn(menu, "alpha.co", t1, &q1) == SCO_OK && SubmitOn(menu, "alpha.co", t2, &q2) == SCO_OK && q2 == q1 + 1);
    d = DoDrain();
    CHECK(d.dispatched == 1 && rec.got.size() == 1 && rec.got[0].text == "two" && Read().done == q2);
    // Never merged: BUTTON, TOGGLE-like kinds (BUTTON twice is two slots) and a different node.
    rec.got.clear();
    auto press = EventFor(b3, 1, SCO_WIDGET_BUTTON);
    CHECK(SubmitOn(menu, "alpha.co", press, &q1) == SCO_OK && SubmitOn(menu, "alpha.co", press, &q2) == SCO_OK && q2 == q1 + 1);
    auto sl3 = EventFor(b3, 2, SCO_WIDGET_SLIDER); sl3.value = 0.1;
    CHECK(SubmitOn(menu, "alpha.co", sl3, &q1) == SCO_OK);
    auto other = EventFor(b3, 4, SCO_WIDGET_TEXT_INPUT); other.text = "x";
    CHECK(SubmitOn(menu, "alpha.co", other, &q2) == SCO_OK && q2 == q1 + 1); // a different node: its own slot
    d = DoDrain();
    CHECK(d.dispatched == 4 && d.dropped == 0 && rec.got.size() == 4 && Read().done == q2); // two buttons, a slider, a text

    // Queue full: 255 buttons, then a slider (the 256th slot). A slider merges when full, a button cannot.
    rec.got.clear();
    const Stamps full0 = Read();
    for (int i = 0; i < 255; ++i) CHECK(SubmitOn(menu, "alpha.co", EventFor(b3, 3, SCO_WIDGET_BUTTON), &seq) == SCO_OK);
    CHECK(SubmitOn(menu, "alpha.co", EventFor(b3, 4, SCO_WIDGET_TEXT_INPUT), &seq) == SCO_OK);
    const uint64_t tailSeq = seq;
    CHECK(SubmitOn(menu, "alpha.co", EventFor(b3, 3, SCO_WIDGET_BUTTON), &seq) == SCO_TOO_MANY);
    auto tt = EventFor(b3, 4, SCO_WIDGET_TEXT_INPUT); tt.text = "late";
    CHECK(SubmitOn(menu, "alpha.co", tt, &seq) == SCO_OK && seq == tailSeq + 1); // the refused button took no number
    CHECK(SubmitOn(menu, "alpha.co", EventFor(b3, 3, SCO_WIDGET_BUTTON), &seq) == SCO_TOO_MANY);
    d = DoDrain();
    CHECK(d.dispatched + d.dropped == 256 && Read().done == tailSeq + 1 && Read().done == full0.done + 257);
    CHECK(rec.got.back().text == "late");
    // And now the queue accepts again, with the next number.
    CHECK(SubmitOn(menu, "alpha.co", EventFor(b3, 3, SCO_WIDGET_BUTTON), &seq) == SCO_OK && seq == tailSeq + 2);
    DoDrain();
    CHECK(table->remove_panel(alpha, "alpha.co") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

static void TestDisabledMergedSlotAndAck() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    auto tree = SliderTree(true);
    CHECK(SetTree(alpha, "alpha.ack", tree, OnEvent, &rec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.ack", b) == SCO_OK);
    auto s = EventFor(b, 2, SCO_WIDGET_SLIDER);
    uint64_t q1 = 0, q2 = 0;
    s.value = 0.2; CHECK(SubmitOn(menu, "alpha.ack", s, &q1) == SCO_OK);
    s.value = 0.7; CHECK(SubmitOn(menu, "alpha.ack", s, &q2) == SCO_OK && q2 == q1 + 1);
    const Stamps before = Read();
    // Ack under delay: a revision bump by the plugin in the gap; events_done stays put until the pass.
    tree[3].label = "T2";
    CHECK(UpdateTree(alpha, "alpha.ack", tree, 3, 1) == SCO_OK);
    Blob gap; CHECK(CopyOn(menu, "alpha.ack", gap) == SCO_OK);
    sco_view_node n{}; CHECK(gap.Node(2, n) && n.value == 0); // the old value
    CHECK(Read().done == before.done && Read().done < q2);
    // The node is disabled before the pass: the merged slot drops once and events_done jumps to its last number.
    tree[1].flags = SCO_WIDGET_DISABLED;
    CHECK(UpdateTree(alpha, "alpha.ack", tree, 1, 1) == SCO_OK);
    const Drain d = DoDrain();
    CHECK(d.dispatched == 0 && d.dropped == 1 && rec.got.empty());
    CHECK(Read().drops == before.drops + 1 && Read().done == q2);
    // The menu snaps to the plugin's value: a copy taken after events_done >= seq shows it.
    Blob after; CHECK(CopyOn(menu, "alpha.ack", after) == SCO_OK);
    CHECK(after.Node(2, n) && n.value == 0 && (n.flags & SCO_WIDGET_DISABLED));
    // A delivered event: the copy taken after seeing events_done >= seq shows the effect.
    tree[1].flags = 0;
    CHECK(UpdateTree(alpha, "alpha.ack", tree, 1, 1) == SCO_OK);
    Blob fresh; CHECK(CopyOn(menu, "alpha.ack", fresh) == SCO_OK);
    s = EventFor(fresh, 2, SCO_WIDGET_SLIDER); s.value = 0.9;
    uint64_t q3 = 0; CHECK(SubmitOn(menu, "alpha.ack", s, &q3) == SCO_OK);
    CHECK(Read().done < q3);
    DoDrain();
    CHECK(Read().done >= q3);
    Blob now; CHECK(CopyOn(menu, "alpha.ack", now) == SCO_OK && now.Node(2, n) && n.value == 0.9);
    CHECK(table->remove_panel(alpha, "alpha.ack") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// Callbacks that change things during a drain.
static void TestMidDrain() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    auto fresh = [&](Rec& rec, bool tagged) {
        auto tree = SliderTree(tagged);
        CHECK(SetTree(alpha, "alpha.mid", tree, OnEvent, &rec) == SCO_OK);
        Blob b; CHECK(CopyOn(menu, "alpha.mid", b) == SCO_OK);
        return b;
    };
    // A callback replaces its own panel: slots 2 and 3 are key-matched. Tagged: delivered; one whose control is gone drops.
    {
        Rec rec; Blob b = fresh(rec, true);
        auto tree = SliderTree(true);
        rec.action = [&](const Got& g) {
            if (g.id != 1) return;
            tree.erase(tree.begin() + 2); // "btn.x" disappears
            tree[0].id = 31; tree[1].id = 32; tree[2].id = 34; // ids renumbered, tags unchanged
            CHECK(SetTree(alpha, "alpha.mid", tree, OnEvent, &rec) == SCO_OK);
        };
        uint64_t q = 0;
        auto s = EventFor(b, 2, SCO_WIDGET_SLIDER); s.value = 0.5;
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 1, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        CHECK(SubmitOn(menu, "alpha.mid", s, &q) == SCO_OK);
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 3, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        const Stamps pre = Read();
        const Drain d = DoDrain();
        CHECK(d.dispatched == 2 && d.dropped == 1 && Read().drops == pre.drops + 1 && Read().done == q);
        CHECK(rec.got.size() == 2 && rec.got[1].id == 32 && rec.got[1].tag == "sld.s"); // by key, with its new id
        CHECK(table->remove_panel(alpha, "alpha.mid") == SCO_OK);
    }
    // A callback calls update_nodes or remove_nodes: no generation break. Untagged: the node it relabels drops, the rest deliver.
    {
        Rec rec; Blob b = fresh(rec, false);
        auto tree = SliderTree(false);
        rec.action = [&](const Got& g) {
            if (g.id != 1) return;
            tree[1].label = "S relabelled";
            CHECK(UpdateTree(alpha, "alpha.mid", tree, 1, 1) == SCO_OK);
        };
        uint64_t q = 0;
        auto s = EventFor(b, 2, SCO_WIDGET_SLIDER); s.value = 0.5;
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 1, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        CHECK(SubmitOn(menu, "alpha.mid", s, &q) == SCO_OK);
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 3, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        const Drain d = DoDrain();
        CHECK(d.dispatched == 2 && d.dropped == 1 && rec.got.size() == 2 && rec.got[1].id == 3 && Read().done == q);
        CHECK(table->remove_panel(alpha, "alpha.mid") == SCO_OK);
    }
    {
        Rec rec; Blob b = fresh(rec, true);
        rec.action = [&](const Got& g) {
            if (g.id != 1) return;
            const uint32_t gone = 3;
            CHECK(table->remove_nodes(alpha, "alpha.mid", &gone, 1) == SCO_OK);
        };
        uint64_t q = 0;
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 1, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 3, SCO_WIDGET_BUTTON), &q) == SCO_OK); // removed by the first callback
        auto tx = EventFor(b, 4, SCO_WIDGET_TEXT_INPUT); tx.text = "ok";
        CHECK(SubmitOn(menu, "alpha.mid", tx, &q) == SCO_OK);
        const Drain d = DoDrain();
        CHECK(d.dispatched == 2 && d.dropped == 1 && rec.got.size() == 2 && rec.got[1].text == "ok");
        // A callback that removes its panel drops the rest.
        CHECK(table->remove_panel(alpha, "alpha.mid") == SCO_OK);
    }
    {
        Rec rec; Blob b = fresh(rec, true);
        rec.action = [&](const Got&) { CHECK(table->remove_panel(alpha, "alpha.mid") == SCO_OK); };
        uint64_t q = 0;
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 1, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 3, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        CHECK(SubmitOn(menu, "alpha.mid", EventFor(b, 3, SCO_WIDGET_BUTTON), &q) == SCO_OK);
        const Drain d = DoDrain();
        CHECK(d.dispatched == 1 && d.dropped == 2 && rec.got.size() == 1 && Read().done == q);
    }
    // Slots that arrive during a drain wait for the next call, and a taken slot is never merged into.
    {
        Rec rec; Blob b = fresh(rec, true);
        uint64_t q = 0, late = 0;
        bool submitted = false;
        rec.action = [&](const Got& g) {
            if (g.id != 2 || submitted) return;
            submitted = true;
            auto s2 = EventFor(b, 2, SCO_WIDGET_SLIDER); s2.value = 0.9; // same key as the slot being handled
            CHECK(SubmitOn(menu, "alpha.mid", s2, &late) == SCO_OK);
            // dispatch_events from inside a widget callback is refused
            const Drain inner = DoDrain();
            CHECK(inner.result == SCO_FAILED);
        };
        auto s = EventFor(b, 2, SCO_WIDGET_SLIDER); s.value = 0.5;
        CHECK(SubmitOn(menu, "alpha.mid", s, &q) == SCO_OK);
        Drain d = DoDrain();
        CHECK(d.dispatched == 1 && rec.got.size() == 1 && late == q + 1 && Read().done == q); // not merged, not finished
        d = DoDrain();
        CHECK(d.dispatched == 1 && rec.got.size() == 2 && rec.got[1].value == 0.9 && Read().done == late);
        CHECK(table->remove_panel(alpha, "alpha.mid") == SCO_OK);
    }
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// A random mix checked against a model of the queue rules.
static void TestProperty() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    auto tree = SliderTree(false);
    tree.push_back(Mk(SCO_WIDGET_SLIDER, 5, 0, "S5")); tree.back().maximum = 100;
    CHECK(SetTree(alpha, "alpha.prop", tree, OnEvent, &rec) == SCO_OK);
    struct MSlot { uint32_t kind, node; uint64_t key, first, last; double value; std::string text; };
    std::vector<MSlot> model;
    Blob b; CHECK(CopyOn(menu, "alpha.prop", b) == SCO_OK);
    double live2 = 0, live5 = 0; std::string liveText;
    uint64_t seqModel = Read().done, done = Read().done, dropsModel = Read().drops;
    uint64_t state = 0x9e3779b97f4a7c15ull;
    auto rnd = [&](uint32_t n) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return static_cast<uint32_t>(state % n); };
    int relabels = 0;
    for (int step = 0; step < 3000; ++step) {
        const uint32_t op = rnd(10);
        if (op <= 6) {
            uint32_t node = 0, kind = 0;
            if (op <= 2) { node = 2; kind = SCO_WIDGET_SLIDER; }
            else if (op <= 4) { node = 5; kind = SCO_WIDGET_SLIDER; }
            else if (op == 5) { node = 4; kind = SCO_WIDGET_TEXT_INPUT; }
            else { node = 1; kind = SCO_WIDGET_BUTTON; }
            auto e = EventFor(b, node, kind);
            const double v = rnd(2) ? 0.5 : (node == 5 ? static_cast<double>(rnd(100)) : static_cast<double>(rnd(2)));
            std::string text = "t" + std::to_string(step);
            if (kind == SCO_WIDGET_SLIDER) e.value = v;
            if (kind == SCO_WIDGET_TEXT_INPUT) e.text = text.c_str();
            uint64_t seq = 0;
            CHECK(SubmitOn(menu, "alpha.prop", e, &seq) == SCO_OK);
            CHECK(seq == seqModel + 1);
            seqModel = seq;
            if (!model.empty() && (kind == SCO_WIDGET_SLIDER || kind == SCO_WIDGET_TEXT_INPUT) && model.back().kind == kind &&
                model.back().node == node && model.back().key == e.node_key) {
                model.back().last = seq; model.back().value = v; model.back().text = text;
            } else model.push_back({kind, node, e.node_key, seq, seq, kind == SCO_WIDGET_SLIDER ? v : 0, text});
        } else if (op == 7 && relabels < 40) {
            ++relabels;
            tree[1].label = "S relabelled " + std::to_string(relabels); // a new key for slider 2
            CHECK(UpdateTree(alpha, "alpha.prop", tree, 1, 1) == SCO_OK);
            live2 = 0; // the replaced node comes back with the plugin's own value
            CHECK(CopyOn(menu, "alpha.prop", b) == SCO_OK);
        } else {
            // drain, and check the model
            uint32_t ok = 0, bad = 0;
            sco_view_node cur{}; CHECK(b.Node(2, cur));
            uint64_t prev = done;
            for (const auto& s : model) {
                CHECK(s.first == prev + 1 && s.last >= s.first); // contiguous ranges
                prev = s.last;
                bool delivered = true;
                if (s.node == 2) delivered = s.key == cur.key; // the live key is the newest copy's key
                if (delivered) {
                    ++ok;
                    if (s.node == 2) live2 = s.value;
                    if (s.node == 5) live5 = s.value;
                    if (s.node == 4) liveText = s.text;
                } else { ++bad; ++dropsModel; }
            }
            if (!model.empty()) done = model.back().last;
            const Drain d = DoDrain();
            CHECK(d.result == SCO_OK && d.dispatched == ok && d.dropped == bad);
            CHECK(Read().done == done && Read().drops == dropsModel);
            model.clear();
            W::Node n2, n5, n4;
            CHECK(LiveNode("alpha.prop", 2, n2) && LiveNode("alpha.prop", 5, n5) && LiveNode("alpha.prop", 4, n4));
            CHECK(n2.value == live2 && n5.value == live5 && n4.text == liveText);
            CHECK(CopyOn(menu, "alpha.prop", b) == SCO_OK);
        }
    }
    DoDrain();
    CHECK(table->remove_panel(alpha, "alpha.prop") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

static void TestDispatchThreadRules() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    uint32_t a = 7, b = 7;
    menu.Run([&] { r = view->dispatch_events(client, &a, &b); });
    CHECK(r == SCO_WRONG_THREAD && a == 7 && b == 7); // not the game thread
    std::thread other([&] { r = view->dispatch_events(client, &a, &b); });
    other.join();
    CHECK(r == SCO_WRONG_THREAD);
    CHECK(view->dispatch_events(client, nullptr, nullptr) == SCO_OK); // both counts may be NULL
    CHECK(view->dispatch_events(nullptr, &a, &b) == SCO_BAD_ARG);
    // submit and read calls from the game thread or an unregistered thread
    sco_view_event e{}; e.size = sizeof(e); e.kind = SCO_WIDGET_BUTTON; e.node_id = 1; e.epoch = 1; e.node_key = 1; e.row = -1;
    uint64_t q = 0;
    CHECK(view->submit_event(client, "alpha.x", &e, &q) == SCO_WRONG_THREAD);
    std::thread unregistered([&] { r = view->submit_event(client, "alpha.x", &e, &q); });
    unregistered.join();
    CHECK(r == SCO_WRONG_THREAD);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

static sco_plugin* guardOwner = nullptr;
static bool CallGuard(const void* owner, const char*, sco::TaskFn thunk, void* ctx) {
    (void)guardOwner;
    try { thunk(ctx); return true; }
    catch (...) {
        CHECK(sco::detail::QuarantineOwner(owner) == Result::Ok);
        if (!sco::ServiceTableHandedOut(owner)) sco::detail::ReleaseFaultResources(owner);
        return false;
    }
}
static void TestFaultAndRelease(const sco_api* api) {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    // A provider whose raw table escaped: its panels stay but its owner is released.
    sco_plugin* retained = sco::host::NewPlugin("retained");
    const uint32_t backing = 123;
    CHECK(api->provide_service(retained, "retained.test", 0x10000, &backing) == SCO_OK);
    const void* out = nullptr; CHECK(api->query_service("retained.test", 0x10000, &out) == SCO_OK);
    unsigned faults = 0;
    auto faulty = [](const char*, const sco_widget_event*, void* p) { ++*static_cast<unsigned*>(p); throw 1; };
    CHECK(SetTree(retained, "retained.main", {Mk(SCO_WIDGET_BUTTON, 1, 0, "Boom")}, faulty, &faults) == SCO_OK);
    Rec rec;
    CHECK(SetTree(alpha, "alpha.safe", {Mk(SCO_WIDGET_BUTTON, 1, 0, "Fine")}, OnEvent, &rec) == SCO_OK);
    Blob bad, good;
    CHECK(CopyOn(menu, "retained.main", bad) == SCO_OK && CopyOn(menu, "alpha.safe", good) == SCO_OK);
    uint64_t q1 = 0, q2 = 0, q3 = 0;
    CHECK(SubmitOn(menu, "retained.main", EventFor(bad, 1, SCO_WIDGET_BUTTON), &q1) == SCO_OK);
    CHECK(SubmitOn(menu, "retained.main", EventFor(bad, 1, SCO_WIDGET_BUTTON), &q2) == SCO_OK);
    CHECK(SubmitOn(menu, "alpha.safe", EventFor(good, 1, SCO_WIDGET_BUTTON), &q3) == SCO_OK);
    sco::SetCalloutGuard(CallGuard);
    const Stamps pre = Read();
    const Drain d = DoDrain();
    sco::SetCalloutGuard(nullptr);
    CHECK(d.result == SCO_CRASHED && faults == 1 && d.dispatched == 1 && d.dropped == 2 && rec.got.size() == 1);
    CHECK(Read().done == q3 && Read().drops == pre.drops + 2 && Read().list > pre.list);
    // The faulted owner's panel answers CRASHED and drops out of the list; the other panel carries on.
    sco_result c = SCO_OK; uint32_t need = 0;
    menu.Run([&] { c = view->copy_panel(client, "retained.main", nullptr, &need); });
    CHECK(c == SCO_CRASHED);
    sco_view_panel_entry rows[8]{}; uint32_t count = 8;
    menu.Run([&] { r = view->list_panels(client, rows, sizeof(rows[0]), &count); });
    bool listed = false, safeListed = false;
    for (uint32_t i = 0; i < count; ++i) { listed |= std::strcmp(rows[i].id, "retained.main") == 0; safeListed |= std::strcmp(rows[i].id, "alpha.safe") == 0; }
    CHECK(r == SCO_OK && !listed && safeListed);
    CHECK(SubmitOn(menu, "alpha.safe", EventFor(good, 1, SCO_WIDGET_BUTTON), &q1) == SCO_OK);
    CHECK(DoDrain().dispatched == 1 && rec.got.size() == 2);
    CHECK(table->remove_panel(alpha, "alpha.safe") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// ---- stop ----
static void TestStop() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    CHECK(SetTree(alpha, "alpha.stop", {Mk(SCO_WIDGET_BUTTON, 1, 0, "Go")}) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.stop", b) == SCO_OK);
    sco::detail::SetRuntimeStopping(true);
    uint32_t caps[SCO_VIEW_CAPS_1_0]{SCO_VIEW_CAPS_1_0}; sco_view_panel_entry rows[2]{}; uint32_t count = 2, need = 0, a = 0, d = 0;
    uint64_t q = 0, x = 0;
    sco_result cap = view->capabilities(client, caps), st = view->stamps(client, &x, &x, &x, &x);
    sco_result lp = SCO_OK, cp = SCO_OK, se = SCO_OK, enter = SCO_OK;
    const sco_view_event e = EventFor(b, 1, SCO_WIDGET_BUTTON);
    menu.Run([&] {
        lp = view->list_panels(client, rows, sizeof(rows[0]), &count);
        cp = view->copy_panel(client, "alpha.stop", nullptr, &need);
        se = view->submit_event(client, "alpha.stop", &e, &q);
    });
    CHECK(cap == SCO_UNAVAILABLE && st == SCO_UNAVAILABLE && lp == SCO_UNAVAILABLE && cp == SCO_UNAVAILABLE && se == SCO_UNAVAILABLE);
    CHECK(view->dispatch_events(client, &a, &d) == SCO_UNAVAILABLE);
    MenuThread late;
    late.Run([&] { enter = view->menu_thread_enter(client); });
    CHECK(enter == SCO_UNAVAILABLE);
    menu.Run([&] { r = view->menu_thread_leave(client); }); CHECK(r == SCO_OK); // leave still works
    sco::detail::SetRuntimeStopping(false);
    CHECK(view->stamps(client, &x, &x, &x, &x) == SCO_OK); // the same static handle is usable again after a restart
    // widgets Stop() ends the client the same way.
    W::Stop();
    CHECK(view->stamps(client, &x, &x, &x, &x) == SCO_UNAVAILABLE && view->capabilities(client, caps) == SCO_UNAVAILABLE);
    CHECK(W::Start() == Result::Ok);
    CHECK(view->stamps(client, &x, &x, &x, &x) == SCO_OK);
    RemoveAll();
}

// Events queued before a widgets Stop and Start are dropped, never delivered to a new panel
// that reuses the id and the tag.
static void TestStopQueue() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    CHECK(SetTree(alpha, "alpha.sq", {Mk(SCO_WIDGET_BUTTON, 1, 0, "Go", "sq.go")}, OnEvent, &rec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.sq", b) == SCO_OK);
    const sco_view_event e = EventFor(b, 1, SCO_WIDGET_BUTTON);
    uint64_t seq = 0;
    CHECK(SubmitOn(menu, "alpha.sq", e, &seq) == SCO_OK);
    W::Stop();
    CHECK(W::Start() == Result::Ok);
    CHECK(SetTree(alpha, "alpha.sq", {Mk(SCO_WIDGET_BUTTON, 1, 0, "Go", "sq.go")}, OnEvent, &rec) == SCO_OK);
    const Stamps before = Read();
    const Drain d = DoDrain();
    CHECK(d.result == SCO_OK && d.dispatched == 0 && d.dropped == 1);
    CHECK(rec.got.empty());
    const Stamps after = Read();
    CHECK(after.drops == before.drops + 1 && after.done >= seq);
    // A fresh event on the new session is delivered.
    Blob b2; CHECK(CopyOn(menu, "alpha.sq", b2) == SCO_OK);
    const sco_view_event e2 = EventFor(b2, 1, SCO_WIDGET_BUTTON);
    CHECK(SubmitOn(menu, "alpha.sq", e2, &seq) == SCO_OK);
    CHECK(DoDrain().dispatched == 1 && rec.got.size() == 1);
    menu.Run([&] { r = view->menu_thread_leave(client); });
    RemoveAll();
}

// Core stops while a drain runs: the slots taken but not handled are counted as drops and
// events_done moves past them. Nothing is lost without a trace, and nothing is delivered.
static void TestStopMidDrain() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    CHECK(SetTree(alpha, "alpha.md", {Mk(SCO_WIDGET_BUTTON, 1, 0, "A", "md.a"), Mk(SCO_WIDGET_BUTTON, 2, 0, "B", "md.b"),
                                      Mk(SCO_WIDGET_BUTTON, 3, 0, "C", "md.c")}, OnEvent, &rec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.md", b) == SCO_OK);
    uint64_t s1 = 0, s2 = 0, s3 = 0;
    CHECK(SubmitOn(menu, "alpha.md", EventFor(b, 1, SCO_WIDGET_BUTTON), &s1) == SCO_OK);
    CHECK(SubmitOn(menu, "alpha.md", EventFor(b, 2, SCO_WIDGET_BUTTON), &s2) == SCO_OK);
    CHECK(SubmitOn(menu, "alpha.md", EventFor(b, 3, SCO_WIDGET_BUTTON), &s3) == SCO_OK);
    rec.action = [&](const Got&) { sco::detail::SetRuntimeStopping(true); };
    const Stamps before = Read();
    const Drain d = DoDrain();
    sco::detail::SetRuntimeStopping(false);
    CHECK(d.result == SCO_UNAVAILABLE);
    CHECK(rec.got.size() == 1); // only the first callback ran
    CHECK(d.dispatched == 1 && d.dropped == 2);
    const Stamps after = Read();
    CHECK(after.drops == before.drops + 2);
    CHECK(after.done == s3); // moved past the two lost slots
    // The queue is empty: a later drain finds nothing to deliver.
    rec.action = nullptr;
    const Drain again = DoDrain();
    CHECK(again.result == SCO_OK && again.dispatched == 0 && again.dropped == 0 && rec.got.size() == 1);
    menu.Run([&] { r = view->menu_thread_leave(client); });
    RemoveAll();
}

// ---- concurrency ----
// One consistent revision: every label of the panel carries the same iteration number.
static std::vector<Spec> NumberedTree(uint64_t n, bool grow) {
    const std::string tag = std::to_string(n);
    std::vector<Spec> t;
    t.push_back(Mk(SCO_WIDGET_LABEL, 1, 0, "n=" + tag));
    t.push_back(Mk(SCO_WIDGET_BUTTON, 2, 0, "b=" + tag, "race.btn"));
    t.push_back(Mk(SCO_WIDGET_LABEL, 3, 0, "m=" + tag));
    if (grow) for (uint32_t i = 0; i < n % 7; ++i) t.push_back(Mk(SCO_WIDGET_LABEL, 10 + i, 0, "n=" + tag));
    t.push_back(Mk(SCO_WIDGET_TEXT_INPUT, 99, 0, "t=" + tag)); t.back().text = tag;
    return t;
}
static bool ConsistentBlob(const Blob& b) {
    if (scv_validate(b.data(), b.bytes) != 0) return false;
    std::string number;
    for (uint32_t i = 0; i < b.H().node_count; ++i) {
        sco_view_node n{};
        if (!scv_node(b.data(), i, &n)) return false;
        const std::string label = b.Str(n.label);
        const std::string value = label.substr(label.find('=') + 1);
        if (number.empty()) number = value;
        if (value != number) return false;
        if (n.kind == SCO_WIDGET_TEXT_INPUT && b.Str(n.text) != number) return false;
    }
    return true;
}
static void TestRace() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    CHECK(SetTree(alpha, "alpha.race", NumberedTree(0, true), OnEvent, &rec) == SCO_OK);
    std::atomic<bool> stop{false}, menuStop{false};
    std::atomic<int> torn{0}, copies{0}, refused{0}, submitted{0};
    std::atomic<uint64_t> lastRevision{0};
    // A third thread that is not the menu thread: counters work, panel reads are refused.
    std::thread spectator([&] {
        while (!stop.load()) {
            Stamps s;
            if (view->stamps(client, &s.global, &s.list, &s.drops, &s.done) != SCO_OK) ++torn;
            sco_view_panel_entry row[2]{}; uint32_t n = 2;
            if (view->list_panels(client, row, sizeof(row[0]), &n) == SCO_WRONG_THREAD) ++refused;
        }
    });
    // The registered menu thread loops copy, validate and submit while the game thread republishes.
    std::thread runner([&] {
        menu.Run([&] {
            Blob b;
            uint64_t sequence = 0;
            while (!menuStop.load()) {
                uint32_t need = 0;
                sco_result c = view->copy_panel(client, "alpha.race", nullptr, &need);
                if (c != SCO_TOO_MANY) continue;
                b.mem.assign((need + 7) / 8 + 8, 0);
                uint32_t cap = static_cast<uint32_t>(b.mem.size() * 8);
                c = view->copy_panel(client, "alpha.race", b.mem.data(), &cap);
                if (c != SCO_OK) continue;
                b.bytes = cap;
                ++copies;
                if (!ConsistentBlob(b)) ++torn;
                if (b.H().revision < lastRevision.load()) ++torn; // a copy never goes back in time
                lastRevision = b.H().revision;
                sco_view_node n{};
                if (b.Node(2, n) && n.kind == SCO_WIDGET_BUTTON) {
                    sco_view_event e{}; e.size = sizeof(e); e.node_id = 2; e.kind = SCO_WIDGET_BUTTON; e.row = -1;
                    e.epoch = b.H().epoch; e.node_key = n.key;
                    if (view->submit_event(client, "alpha.race", &e, &sequence) == SCO_OK) ++submitted;
                }
            }
        });
    });
    for (uint64_t i = 1; i <= 3000; ++i) {
        const auto tree = NumberedTree(i, true);
        if (i % 2) CHECK(SetTree(alpha, "alpha.race", tree, OnEvent, &rec) == SCO_OK);
        else { CHECK(table->remove_panel(alpha, "alpha.race") == SCO_OK); CHECK(SetTree(alpha, "alpha.race", tree, OnEvent, &rec) == SCO_OK); }
        uint32_t ok = 0, bad = 0;
        view->dispatch_events(client, &ok, &bad);
    }
    menuStop = true;
    runner.join();
    stop = true;
    spectator.join();
    view->dispatch_events(client, nullptr, nullptr);
    CHECK(torn == 0);
    CHECK(copies > 100 && refused > 0 && submitted > 0);
    CHECK(rec.got.size() > 0 && rec.got.size() <= static_cast<size_t>(submitted)); // tagged: delivered at most once each
    CHECK(table->remove_panel(alpha, "alpha.race") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// 10 000 BUTTON events across republishes, tagged and untagged, identical and relabelled.
static void RunRepublishStress(bool tagged, bool relabel, int events) {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    auto tree = [&](int n) {
        std::vector<Spec> t;
        t.push_back(Mk(SCO_WIDGET_ROW, 1, 0, "Row"));
        t.push_back(Mk(SCO_WIDGET_BUTTON, 10, 1, relabel ? "Apply " + std::to_string(n) : "Apply", tagged ? "stress.apply" : ""));
        t.push_back(Mk(SCO_WIDGET_BUTTON, 11, 1, "Delete", tagged ? "stress.delete" : ""));
        return t;
    };
    CHECK(SetTree(alpha, "alpha.stress", tree(0), OnEvent, &rec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.stress", b) == SCO_OK);
    const sco_view_event e = EventFor(b, 10, SCO_WIDGET_BUTTON);
    const Stamps start = Read();
    std::atomic<int> sent{0};
    std::atomic<uint64_t> lastSeq{0};
    std::atomic<bool> sending{true};
    std::thread runner([&] {
        menu.Run([&] {
            for (int i = 0; i < events; ++i) {
                uint64_t seq = 0;
                sco_result s;
                while ((s = view->submit_event(client, "alpha.stress", &e, &seq)) == SCO_TOO_MANY) std::this_thread::yield();
                if (s != SCO_OK) break;
                lastSeq = seq; ++sent;
            }
            sending = false;
        });
    });
    uint64_t dispatched = 0, dropped = 0;
    int round = 0;
    for (;;) {
        ++round;
        CHECK(SetTree(alpha, "alpha.stress", tree(round), OnEvent, &rec) == SCO_OK);
        uint32_t ok = 0, bad = 0;
        view->dispatch_events(client, &ok, &bad);
        dispatched += ok; dropped += bad;
        if (!sending.load()) { // one more pass after the sender finished
            view->dispatch_events(client, &ok, &bad);
            dispatched += ok; dropped += bad;
            break;
        }
    }
    runner.join();
    CHECK(sent == events);
    const Stamps end = Read();
    CHECK(end.done == lastSeq.load());
    // Slots, not events: BUTTONs are never merged, so every event is one slot.
    CHECK(dispatched + dropped == static_cast<uint64_t>(events));
    if (relabel && !tagged) { CHECK(dispatched == 0 && dropped == static_cast<uint64_t>(events) && rec.got.empty()); }
    else { CHECK(dropped == 0 && dispatched == static_cast<uint64_t>(events) && end.drops == start.drops); }
    for (const auto& g : rec.got) { CHECK(g.id == 10); if (g.id != 10) break; } // never the neighbour
    CHECK(table->remove_panel(alpha, "alpha.stress") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}
// update_nodes of other nodes in a loop never drops an event for an untouched node.
static void TestUpdateStress() {
    MenuThread menu;
    sco_result r = SCO_OK;
    menu.Run([&] { r = view->menu_thread_enter(client); });
    Rec rec;
    std::vector<Spec> t;
    t.push_back(Mk(SCO_WIDGET_BUTTON, 1, 0, "Go"));
    t.push_back(Mk(SCO_WIDGET_TOGGLE, 2, 0, "Tog"));
    t.push_back(Mk(SCO_WIDGET_COMBO, 3, 0, "Cmb")); t.back().items = {"A", "B"};
    t.push_back(Mk(SCO_WIDGET_LIST, 4, 0, "Lst")); t.back().items = {"A", "B"};
    t.push_back(Mk(SCO_WIDGET_LABEL, 5, 0, "Churn 0"));
    CHECK(SetTree(alpha, "alpha.churn", t, OnEvent, &rec) == SCO_OK);
    Blob b; CHECK(CopyOn(menu, "alpha.churn", b) == SCO_OK);
    constexpr int kEvents = 4000;
    std::atomic<bool> sending{true};
    std::thread runner([&] {
        menu.Run([&] {
            for (int i = 0; i < kEvents; ++i) {
                sco_view_event e{};
                switch (i % 4) {
                    case 0: e = EventFor(b, 1, SCO_WIDGET_BUTTON); break;
                    case 1: e = EventFor(b, 2, SCO_WIDGET_TOGGLE); e.value = (i / 4) % 2; break;
                    case 2: e = EventFor(b, 3, SCO_WIDGET_COMBO); e.index = (i / 4) % 2; break;
                    default: e = EventFor(b, 4, SCO_WIDGET_LIST); e.index = (i / 4) % 2; break;
                }
                uint64_t seq = 0;
                while (view->submit_event(client, "alpha.churn", &e, &seq) == SCO_TOO_MANY) std::this_thread::yield();
            }
            sending = false;
        });
    });
    uint64_t dispatched = 0, dropped = 0;
    for (int round = 1;; ++round) {
        t[4].label = "Churn " + std::to_string(round);
        CHECK(UpdateTree(alpha, "alpha.churn", t, 4, 1) == SCO_OK);
        uint32_t ok = 0, bad = 0;
        view->dispatch_events(client, &ok, &bad);
        dispatched += ok; dropped += bad;
        if (!sending.load()) { view->dispatch_events(client, &ok, &bad); dispatched += ok; dropped += bad; break; }
    }
    runner.join();
    CHECK(dispatched == kEvents && dropped == 0 && rec.got.size() == static_cast<size_t>(kEvents));
    CHECK(table->remove_panel(alpha, "alpha.churn") == SCO_OK);
    menu.Run([&] { r = view->menu_thread_leave(client); });
}

// The menu thread keeps calling while the control thread runs the stop order; the client is static.
static void TestStopVersusCopy() {
    for (int round = 0; round < 20; ++round) {
        MenuThread menu;
        sco_result r = SCO_OK;
        menu.Run([&] { r = view->menu_thread_enter(client); });
        Rec rec;
        CHECK(SetTree(alpha, "alpha.sv", NumberedTree(1, false), OnEvent, &rec) == SCO_OK);
        std::atomic<bool> stopped{false}, go{false};
        std::atomic<int> badCodes{0}, afterStop{0};
        std::atomic<int> started{0};
        std::thread runner([&] {
            menu.Run([&] {
                Blob b;
                for (int i = 0; i < 200000; ++i) {
                    ++started;
                    const bool wasStopped = stopped.load();
                    uint32_t need = 0;
                    sco_result c = view->copy_panel(client, "alpha.sv", nullptr, &need);
                    sco_view_event e{}; e.size = sizeof(e); e.node_id = 2; e.kind = SCO_WIDGET_BUTTON; e.row = -1; e.epoch = 1; e.node_key = 1;
                    uint64_t seq = 0;
                    sco_result s = view->submit_event(client, "alpha.sv", &e, &seq);
                    uint64_t x = 0;
                    sco_result t = view->stamps(client, &x, &x, &x, &x);
                    const auto known = [](sco_result v) { return v == SCO_OK || v == SCO_TOO_MANY || v == SCO_UNAVAILABLE || v == SCO_NOT_FOUND; };
                    if (!known(c) || !known(s) || !known(t)) ++badCodes;
                    if (wasStopped) { ++afterStop; if (c != SCO_UNAVAILABLE || s != SCO_UNAVAILABLE || t != SCO_UNAVAILABLE) ++badCodes; if (afterStop > 50) break; }
                }
                (void)view->menu_thread_leave(client);
            });
        });
        while (started.load() < 50) std::this_thread::yield();
        W::Stop(); // Core stop closes admission first
        stopped = true;
        runner.join();
        CHECK(badCodes == 0 && afterStop > 0);
        CHECK(view->stamps(client, nullptr, nullptr, nullptr, nullptr) == SCO_UNAVAILABLE);
        CHECK(W::Start() == Result::Ok);
    }
}

int main() {
    sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"test-widgets-view 1.0"});
    alpha = sco::host::NewPlugin("alpha");
    CHECK(W::Start() == Result::Ok);
    table = W::Table();
    view = V::ViewTable();
    client = V::ViewClient();
    TestHandshakeAndPins(api);
    TestRegistration();
    TestCopyAndList();
    TestKeyVectors();
    TestRevisions();
    TestRepublishTable();
    TestParityWithRenderPanel();
    TestSubmitShape();
    TestCoalescing();
    TestDisabledMergedSlotAndAck();
    TestMidDrain();
    TestProperty();
    TestDispatchThreadRules();
    TestFaultAndRelease(api);
    TestStop();
    TestStopQueue();
    TestStopMidDrain();
    TestRace();
    RunRepublishStress(true, false, 10000);   // tagged, identical republish: every event delivered
    RunRepublishStress(false, false, 10000);  // untagged, identical republish: the same
    RunRepublishStress(true, true, 10000);    // tagged, label changed every time: the tag keeps it clickable
    RunRepublishStress(false, true, 10000);   // untagged, label changed every time: all dropped, none misdelivered
    TestUpdateStress();
    TestStopVersusCopy();
    W::Stop();
    std::printf("widgets_view: %d passed, %d failed\n", passed.load(), failed.load());
    return failed ? 1 : 0;
}
