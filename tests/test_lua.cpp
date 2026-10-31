// Tests for sco-lua (plugins/lua) loaded through the real plugin loader and the real host table:
// Discover -> LoadScript -> sco_lua_load, with sco::host::BuildApi's sco_api underneath. Host
// build, no game. The "game thread" is this test's main thread.
//   tools/test.sh
#include "sco/caps.h"
#ifndef SCO_KERNEL_ONLY
#include "game_contracts/lua_datacore_headers.inc"
#endif
#include "sco/host.h"
#include "sco/log.h"
#include "sco/plugins.h"
#include "sco/runtime.h"
#include "sco/settings.h"
#include "sco/status.h"
#include "sco/storage.h"
#include "sco/ui.h"
#include "sco/widgets.h"
#include "../plugins/lua/sco_lua.h"
#include "../plugins/lua/sco_lua_ext.h"   // the extension registry (TestExtensions)
#ifndef SCO_KERNEL_ONLY   // the game adapter (plugins/lua/sco_lua_game.c) and the tables it reads
#include "game_contracts/lua_adapter_headers.inc"
#endif
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace P = sco::plugins;
using sco::Result;
using P::State;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::vector<std::string> g_log;
static void Sink(const char* line) { g_log.emplace_back(line); }
static bool Logged(const char* needle) {
    for (const auto& l : g_log) if (l.find(needle) != std::string::npos) return true;
    return false;
}

static const P::ScriptRuntime kLua{ sco_lua_load, sco_lua_unload };
static const sco_api* g_api;
static fs::path g_root;

// Writes data/plugins/<id>/{plugin.ini,main.lua} under the test root.
static void Write(const std::string& id, const std::string& lua, const std::string& extraIni = "") {
    const fs::path dir = g_root / id;
    fs::create_directories(dir);
    std::ofstream(dir / "plugin.ini") << "id = " << id << "\nname = " << id << "\nversion = 1.0\napi = 1.0\nkind = lua\nentry = main.lua\n" << extraIni;
    std::ofstream(dir / "main.lua", std::ios::binary) << lua;
}

struct Loaded {
    std::vector<P::Plugin> list;
    P::Plugin* p = nullptr;
};

// Discovers the root and loads plugin `id` (a fresh handle each time).
static Loaded Load(const std::string& id, const sco_api* api = nullptr) {
    Loaded l;
    P::Options opts;
    opts.enabled = true;
    l.list = P::Discover(g_root, opts);
    for (auto& p : l.list) if (p.folder == id) l.p = &p;
    if (l.p && l.p->state == State::Ready) {
        sco_plugin* self = sco::host::NewPlugin(id.c_str());
        if (!l.p->manifest.settings.empty()) sco::settings::Declare(self, l.p->manifest.settings);   // as sco::app::Start does
        P::LoadScript(*l.p, api ? api : g_api, self, kLua);
    }
    return l;
}

static void Unload(Loaded& l) { P::UnloadAll(l.list, P::PlatformModuleOps(), &kLua); }

// The script-only leg of app::Reload: same discovery record, administrative gate, fresh owner.
static bool ReloadScript(Loaded& l) {
    if (!l.p) return false;
    sco_plugin* old = l.p->self;
    P::UnloadOne(*l.p, P::PlatformModuleOps(), &kLua);
    CHECK(l.p->state == State::Unloaded && !sco_lua_alive(old));
    if (l.p->state != State::Unloaded) return false;
    l.p->state = State::Ready;
    sco_plugin* next = sco::host::NewPlugin(l.p->manifest.id.c_str());
    CHECK(next != old);
    return P::LoadScript(*l.p, g_api, next, kLua);
}

struct Reply { bool called = false; sco_result r = SCO_OK; std::string text; };
static void OnDone(sco_result r, const char* reply, void* ctx) {
    auto* d = static_cast<Reply*>(ctx);
    d->called = true; d->r = r; d->text = reply ? reply : "";
}
static Reply Invoke(sco_plugin* by, const char* name, std::vector<sco_arg> args = {}) {
    Reply d;
    g_api->invoke(by, name, args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), OnDone, &d);
    return d;
}
static sco_arg Str(const char* s) { sco_arg a{}; a.type = SCO_ARG_STRING; a.v.s = s; return a; }
static sco_arg Bool(bool b) { sco_arg a{}; a.type = SCO_ARG_BOOL; a.v.i = b; return a; }
static sco_arg Int(int64_t i) { sco_arg a{}; a.type = SCO_ARG_INT; a.v.i = i; return a; }

static bool HasCommand(const char* name) {
    const sco_command* list[512];
    const uint32_t n = g_api->list_commands(list, 512);
    for (uint32_t i = 0; i < n; ++i) if (strcmp(list[i]->name, name) == 0) return true;
    return false;
}

static sco_plugin* g_caller;   // a second plugin that invokes the scripts' commands

// ---- the SDK's own example, sdk/examples/greeter ---------------------------------------------

class LuaWidgetRenderer final : public sco::widgets::Renderer {
public:
    std::vector<sco::widgets::Event> events;
    void Render(const sco::widgets::Panel&, std::vector<sco::widgets::Event>& out) override { out = events; }
};

static void TestWidgets() {
    Write("widgetsnone", R"lua(
        assert(type(sco.widgets) == "table")
        assert(not sco.widgets.available())
        assert(select(2, sco.widgets.set_panel("widgetsnone.main", "Example", {
          {kind="label", id=1, label="Hello"}})) == "unavailable")
        assert(select(2, sco.widgets.remove_panel("widgetsnone.main")) == "unavailable")
    )lua");
    Loaded none = Load("widgetsnone");
    CHECK(none.p && none.p->state == State::Loaded);
    if (none.p && none.p->state != State::Loaded) std::printf("  widgetsnone: %s\n", none.p->reason.c_str());
    Unload(none);
    CHECK(sco::widgets::Start() == Result::Ok);
    Write("widgetslua", R"lua(
        assert(sco.widgets.available())
        assert(sco.widgets.set_panel("widgetslua.main", "Example", {
          {kind="label", id=1, label="Hello"}}))
    )lua");
    Loaded live = Load("widgetslua");
    CHECK(live.p && live.p->state == State::Loaded);
    if (live.p && live.p->state != State::Loaded) std::printf("  widgetslua: %s\n", live.p->reason.c_str());
    const auto panels = sco::widgets::Panels();
    CHECK(panels.size() == 1 && panels[0].nodes.size() == 1 && panels[0].nodes[0].label == "Hello");
    Unload(live);
    CHECK(sco::widgets::Panels().empty());
    sco::widgets::Stop();
}

static void TestWidgetsTypedEvents() {
    CHECK(sco::widgets::Start() == Result::Ok);
    Write("widgetstyped", R"lua(
        local w, seen = sco.widgets, {}
        local nodes = {
          {kind="column", id=1}, {kind="row", id=2, parent=1},
          {kind="label", id=3, parent=2, label="Plain ## text"},
          {kind="button", id=4, parent=2, label="Press"},
          {kind="toggle", id=5, parent=1, label="Enabled", value=false},
          {kind="slider", id=6, parent=1, label="Speed", value=0.5, minimum=0, maximum=1},
          {kind="combo", id=7, parent=1, label="Mode", items={"Alpha", "Beta"}, index=0},
          {kind="list", id=8, parent=1, label="List", items={"One", "Two"}, index=-1},
          {kind="text_input", id=9, parent=1, label="Name", text="Initial"},
          {kind="table", id=10, parent=1, items={"A", "B", "C", "D"}, columns=2},
          {kind="button", id=11, label="Disabled", disabled=true}
        }
        assert(w.set_panel("widgetstyped.main", "Typed", nodes, function(panel, event)
          assert(panel == "widgetstyped.main")
          assert(type(event.node_id) == "number" and type(event.kind) == "string")
          if event.kind == "button" then
            assert(event.value == nil and event.index == nil and event.text == nil)
            assert(sco.invoke("widgetstyped.ping"))
          elseif event.kind == "toggle" then
            assert(event.value == true and type(event.value) == "boolean")
          elseif event.kind == "slider" then assert(event.value == 0.75)
          elseif event.kind == "combo" then assert(event.index == 1)
          elseif event.kind == "list" then assert(event.index == 0)
          elseif event.kind == "text_input" then assert(event.text == "Edited")
          else error("unexpected event kind") end
          seen[#seen+1] = event.kind
        end))
        nodes[3].label="mutated"; nodes[7].items[1]="mutated"; nodes[9].text="mutated"
        nodes = nil
        for i=1,200 do local garbage = string.rep("x", 1024) end
        assert(sco.register_command{name="widgetstyped.ping", title="Ping", fn=function() return "pong" end})
        assert(sco.register_command{name="widgetstyped.seen", title="Seen", fn=function() return table.concat(seen, ",") end})
        assert(sco.register_command{name="widgetstyped.remove", title="Remove", fn=function()
          assert(w.remove_panel("widgetstyped.main"))
          assert(select(2, w.remove_panel("widgetstyped.main")) == "not_found")
          return "removed" end})
    )lua");
    Loaded live = Load("widgetstyped");
    CHECK(live.p && live.p->state == State::Loaded);
    if (live.p && live.p->state != State::Loaded) std::printf("  widgetstyped: %s\n", live.p->reason.c_str());
    const auto panels = sco::widgets::Panels();
    CHECK(panels.size() == 1 && panels[0].nodes.size() == 11);
    if (!panels.empty() && panels[0].nodes.size() == 11) {
        CHECK(panels[0].nodes[2].label == "Plain ## text" && panels[0].nodes[6].items[0] == "Alpha" && panels[0].nodes[8].text == "Initial");
        CHECK(panels[0].nodes[10].flags == SCO_WIDGET_DISABLED && panels[0].nodes[9].columns == 2);
    }
    LuaWidgetRenderer renderer;
    renderer.events = {{4,SCO_WIDGET_BUTTON}, {5,SCO_WIDGET_TOGGLE,0,1}, {6,SCO_WIDGET_SLIDER,0,0.75},
                       {7,SCO_WIDGET_COMBO,1}, {8,SCO_WIDGET_LIST,0}, {9,SCO_WIDGET_TEXT_INPUT,0,0,"Edited"},
                       {11,SCO_WIDGET_BUTTON}};
    CHECK(sco::widgets::RenderPanel("widgetstyped.main", renderer) == Result::Ok);
    CHECK(Invoke(g_caller, "widgetstyped.seen").text == "button,toggle,slider,combo,list,text_input");
    CHECK(Invoke(g_caller, "widgetstyped.remove").text == "removed");
    CHECK(sco::widgets::Panels().empty());
    Unload(live);
    sco::widgets::Stop();
}

static const sco_widgets_v1* g_widgetsFixture = nullptr;
static uint32_t g_widgetsMaxVersion = SCO_WIDGETS_VERSION_1_2;
static sco_result g_widgetsQueryError = SCO_OK;
static sco_result g_widgetsMissingResult = SCO_NOT_FOUND;
static std::vector<uint32_t> g_widgetsVersions;
static uint32_t g_widgetsFeatureMask = UINT32_MAX;
static uint32_t g_widgetsCapsCount = 0;
static sco_result WidgetFixtureCapabilities(sco_plugin* self, uint32_t* caps) {
    const auto r = sco::widgets::Table()->capabilities(self, caps);
    if (r == SCO_OK) {
        caps[SCO_WIDGETS_CAP_FEATURES] &= g_widgetsFeatureMask;
        if (g_widgetsCapsCount) caps[0] = g_widgetsCapsCount;
    }
    return r;
}
static void TestPanelBuilder() {
    CHECK(sco::widgets::Start() == Result::Ok);
    Write("panelbuilder", R"lua(
      local M, seen = sco.panel_builder, {}
      assert(type(M)=="table" and type(M.new)=="function")
      assert(require==nil and package==nil and load==nil and io==nil and os==nil and debug==nil)
      assert(not pcall(setmetatable, {}, {__gc=function() end}))
      local b = M.new()
      assert(not pcall(function() b:build() end))
      local function changed(panel, event)
        assert(panel=="panelbuilder.main")
        if event.kind=="toggle" then assert(event.value==true)
        else assert(event.kind=="slider" and event.value==0.75) end
        seen[#seen+1]=event.kind
      end
      local weak = setmetatable({changed=changed}, {__mode="v"})
      local options = {label="Same", value=false, on_change=changed}
      assert(b:column(function(ui)
        assert(ui==b)
        assert(not pcall(function() ui:build() end))
        assert(ui:row(function(ui)
          assert(ui:toggle(options)==3)
          assert(ui:slider{label="Same", value=0.5, minimum=0, maximum=1, on_change=changed}==4)
        end)==2)
        assert(ui:button{label="Same", on_click=function(panel, event)
          assert(panel=="panelbuilder.main" and event.kind=="button" and event.value==nil)
          seen[#seen+1]="button"
        end}==5)
        ui:column(function(ui)
          ui:button{label="Disabled", on_click=function() error("disabled callback") end}
        end, {disabled=true})
      end)==1)
      options.label="mutated"; options.on_change=function() error("mutated callback") end
      b:button{label="Root", disabled=true, on_click=function() error("disabled leaf") end}
      local nodes, dispatch = b:build()
      local parents = {0,1,2,2,1,1,6,0}
      assert(#nodes==8)
      for i,n in ipairs(nodes) do
        assert(n.id==i and n.parent==parents[i])
        assert(n.on_change==nil and n.on_click==nil)
      end
      assert(nodes[3].label=="Same" and nodes[3].value==false)
      assert(not dispatch("panelbuilder.main", {node_id=999, kind="button"}))
      assert(not dispatch("panelbuilder.main", {node_id=3, kind="button"}))
      assert(not dispatch("panelbuilder.main", {node_id=7, kind="button"}))
      assert(not dispatch("panelbuilder.main", {node_id=8, kind="button"}))
      assert(b:button{label="Future", on_click=function() error("future callback") end}==9)
      assert(not dispatch("panelbuilder.main", {node_id=9, kind="button"}))
      nodes[3].label="snapshot changed"
      local fresh = b:build()
      assert(#fresh==9 and fresh[3].label=="Same" and fresh~=nodes and fresh[3]~=nodes[3])
      nodes[3].label="Same"
      assert(sco.widgets.set_panel("panelbuilder.main", "Builder", nodes, dispatch))
      b=nil; nodes=nil; fresh=nil; dispatch=nil; changed=nil; options=nil
      sco.widget_test_gc(); assert(weak.changed~=nil)
      assert(sco.register_command{name="panelbuilder.seen", title="Seen", fn=function()
        return table.concat(seen, ",") end})
      assert(sco.register_command{name="panelbuilder.remove", title="Remove", fn=function()
        assert(sco.widgets.remove_panel("panelbuilder.main"))
        sco.widget_test_gc()
        assert(weak.changed==nil)
        return "removed" end})
    )lua");
    Loaded live = Load("panelbuilder");
    CHECK(live.p && live.p->state == State::Loaded);
    if (live.p && live.p->state != State::Loaded) std::printf("  panelbuilder: %s\n", live.p->reason.c_str());
    const auto panels = sco::widgets::Panels();
    CHECK(panels.size() == 1 && panels[0].nodes.size() == 8);
    if (panels.size() == 1 && panels[0].nodes.size() == 8) {
        const uint32_t parents[] = {0,1,2,2,1,1,6,0};
        for (size_t i = 0; i < 8; ++i)
            CHECK(panels[0].nodes[i].id == i + 1 && panels[0].nodes[i].parent == parents[i]);
        CHECK(panels[0].nodes[2].label == "Same" && panels[0].nodes[3].value == 0.5);
    }
    LuaWidgetRenderer renderer;
    renderer.events = {{3,SCO_WIDGET_TOGGLE,0,1}, {4,SCO_WIDGET_SLIDER,0,0.75},
                       {5,SCO_WIDGET_BUTTON}, {7,SCO_WIDGET_BUTTON}, {8,SCO_WIDGET_BUTTON}};
    CHECK(sco::widgets::RenderPanel("panelbuilder.main", renderer) == Result::Ok);
    CHECK(Invoke(g_caller, "panelbuilder.seen").text == "toggle,slider,button");
    CHECK(Invoke(g_caller, "panelbuilder.remove").text == "removed");
    CHECK(sco::widgets::Panels().empty());
    Unload(live);

    Write("builderinvalid", R"lua(
      local b = sco.panel_builder.new()
      local function refused(fn) assert(not pcall(fn)) end
      for _,options in ipairs{
        {}, {label=""}, {label=1}, {label="x\0y"}, {label="\192\128"},
        {label=string.rep("x",256)}, {label="x", disabled=1}, {label="x", on_click=true},
        {label="x", id=1}, {label="x", parent=99}, {label="x", kind="toggle"}
      } do refused(function() b:button(options) end) end
      refused(function() b:button(nil) end)
      refused(function() b:toggle{label="x", value=1} end)
      for _,value in ipairs{0/0, math.huge, -math.huge, -1, 2, "0.5"} do
        refused(function() b:slider{label="x", value=value, minimum=0, maximum=1} end)
      end
      refused(function() b:slider{label="x", value=0} end)
      refused(function() b:slider{label="x", value=0, minimum=1, maximum=0} end)
      refused(function() b:slider{label="x", value=0, minimum=0, maximum=math.huge} end)
      refused(function() b:row(nil) end)
      refused(function() b:row(function() end, false) end)
      refused(function() b:row(function() end, {value=1}) end)
      local hidden = setmetatable({}, {__index=function() error("metamethod called") end})
      refused(function() b:button(hidden) end)
      assert(b:button{label=string.rep("x",255)}==1)
      refused(function() b:column(function(ui)
        ui:row(function(ui) ui:button{label="rolled back", on_click=function() error("stale") end} end)
        error("abort scope")
      end) end)
      assert(b:row(function(ui) assert(ui:toggle{label="Default"}==3) end)==2)
      assert(b:slider{label="Fixed", value=1, minimum=1, maximum=1}==4)
      local nodes, dispatch = b:build()
      assert(#nodes==4 and nodes[3].parent==2 and nodes[3].value==false and nodes[4].parent==0)
      assert(not dispatch("builderinvalid.main", {node_id=3, kind="toggle", value=true}))
      assert(sco.widgets.set_panel("builderinvalid.main", "Recovered", nodes, dispatch))
      local other = sco.panel_builder.new()
      for i=1,256 do assert(other:button{label="x"}==i) end
      refused(function() other:button{label="overflow"} end)
      assert(#other:build()==256)
      assert(sco.widgets.set_panel("builderinvalid.limit", "Limit", other:build()))
    )lua");
    Loaded invalid = Load("builderinvalid");
    CHECK(invalid.p && invalid.p->state == State::Loaded);
    if (invalid.p && invalid.p->state != State::Loaded) std::printf("  builderinvalid: %s\n", invalid.p->reason.c_str());
    CHECK(sco::widgets::Panels().size() == 2);
    Unload(invalid);
    CHECK(sco::widgets::Panels().empty());

    Write("builderreplace", R"lua(
      local b = sco.panel_builder.new()
      b:button{label="Replace", on_click=function(panel)
        local replacement = sco.panel_builder.new()
        replacement:button{label="Remove", on_click=function(panel)
          assert(sco.widgets.remove_panel(panel))
          sco.widget_test_gc()
          sco.log("info", "builder removed")
        end}
        assert(sco.widgets.set_panel(panel, "Replaced", replacement:build()))
        sco.widget_test_gc()
        sco.log("info", "builder replaced")
      end}
      assert(sco.widgets.set_panel("builderreplace.main", "Original", b:build()))
    )lua");
    Loaded replaced = Load("builderreplace");
    CHECK(replaced.p && replaced.p->state == State::Loaded);
    renderer.events = {{1,SCO_WIDGET_BUTTON}, {1,SCO_WIDGET_BUTTON}};
    CHECK(sco::widgets::RenderPanel("builderreplace.main", renderer) == Result::Ok);
    CHECK(Logged("[builderreplace] builder replaced") && sco::widgets::Panels().size() == 1);
    CHECK(sco::widgets::RenderPanel("builderreplace.main", renderer) == Result::Ok);
    CHECK(Logged("[builderreplace] builder removed") && sco::widgets::Panels().empty());
    Unload(replaced);

    Write("builderspin", R"lua(
      local b = sco.panel_builder.new()
      b:button{label="Spin", on_click=function()
        sco.log("info", "builder spin entered")
        while true do end
      end}
      assert(sco.widgets.set_panel("builderspin.main", "Budget", b:build()))
    )lua");
    Loaded spin = Load("builderspin");
    CHECK(spin.p && spin.p->state == State::Loaded);
    CHECK(sco::widgets::RenderPanel("builderspin.main", renderer) == Result::Ok);
    CHECK(spin.p && !sco_lua_alive(spin.p->self));
    CHECK(Logged("[builderspin] error: script disabled: ran past its step budget"));
    Unload(spin);
    CHECK(sco::widgets::Panels().empty());
    sco::widgets::Stop();
}

static sco_result WidgetsFixtureQuery(const char* name, uint32_t version, const void** out) {
    CHECK(strcmp(name, SCO_WIDGETS_NAME) == 0 && version >= SCO_WIDGETS_VERSION_1_0 && version <= SCO_WIDGETS_VERSION_1_3);
    g_widgetsVersions.push_back(version);
    *out = nullptr;
    if (g_widgetsQueryError != SCO_OK) return g_widgetsQueryError;
    if (version > g_widgetsMaxVersion) return g_widgetsMissingResult;
    *out = g_widgetsFixture;
    return SCO_OK;
}

static void TestWidgetsServiceTables() {
    CHECK(sco::widgets::Start() == Result::Ok);
    sco_api api = *g_api;
    api.query_service = WidgetsFixtureQuery;
    const char* missing = R"lua(
      assert(not sco.widgets.available())
      assert(select(2,sco.widgets.set_panel("widgetstables.main","Test",{{kind="label",id=1,label="Test"}})) == "unavailable")
      assert(select(2,sco.widgets.remove_panel("widgetstables.main")) == "unavailable")
    )lua";
    Write("widgetstables", missing);
    auto probe = [&] {
        Loaded l = Load("widgetstables", &api);
        CHECK(l.p && l.p->state == State::Loaded);
        if (l.p && l.p->state != State::Loaded) std::printf("  widgetstables: %s\n", l.p->reason.c_str());
        Unload(l);
    };
    api.size = static_cast<uint32_t>(offsetof(sco_api, query_service)); probe();
    api.size = static_cast<uint32_t>(offsetof(sco_api, query_service) + sizeof(api.query_service) - 1); probe();
    api.size = sizeof(api); api.query_service = nullptr; probe();
    api.query_service = WidgetsFixtureQuery; g_widgetsFixture = nullptr; probe();
    sco_widgets_v1 table = *sco::widgets::Table();
    g_widgetsFixture = &table;
    table.size = sizeof(uint32_t); probe();
    table.size = static_cast<uint32_t>(offsetof(sco_widgets_v1, set_panel) + sizeof(table.set_panel) - 1); probe();
    void* partial=std::malloc(table.size); std::memcpy(partial,&table,table.size);
    g_widgetsFixture=static_cast<const sco_widgets_v1*>(partial); probe(); std::free(partial);
    g_widgetsFixture=&table;
    table = *sco::widgets::Table(); table.set_panel = nullptr; table.remove_panel = nullptr; probe();
    table = *sco::widgets::Table();
    Write("widgetstables", R"lua(
      assert(not sco.widgets.available())
      assert(sco.widgets.set_panel("widgetstables.main","Test",{{kind="label",id=1,label="Test"}}))
      assert(select(2,sco.widgets.remove_panel("widgetstables.main")) == "unavailable")
    )lua");
    table.size = static_cast<uint32_t>(offsetof(sco_widgets_v1, remove_panel) + sizeof(table.remove_panel) - 1); probe();
    partial=std::malloc(table.size); std::memcpy(partial,&table,table.size);
    g_widgetsFixture=static_cast<const sco_widgets_v1*>(partial); probe(); std::free(partial);
    g_widgetsFixture=&table;
    table.size = sizeof(table); table.remove_panel = nullptr; probe();
    CHECK(sco::widgets::Panels().empty());
    g_widgetsFixture = nullptr;
    sco::widgets::Stop();
}

static void TestWidgetsInvalid() {
    CHECK(sco::widgets::Start() == Result::Ok);
    Write("widgetsbad", R"lua(
      local w=sco.widgets
      local good={{kind="button",id=1,label="Original"}}
      assert(w.set_panel("widgetsbad.main","Original",good,function() sco.log("info","original callback") end))
      local function bad(nodes, expected, id, title, fn)
        local ok,err=w.set_panel(id or "widgetsbad.main",title or "Invalid",nodes,fn)
        assert(not ok and err==(expected or "bad_arg"),tostring(err))
      end
      bad({}); bad(nil); bad({true}); bad({{kind="other",id=1}})
      bad({{kind="label",id=0,label="x"}}); bad({{kind="label",id=2^32,label="x"}})
      bad({{kind="label",id=1.0,label="x"}}); bad({{kind="label",label="x"}})
      bad({{kind="button",id=1,label=""}})
      bad({{kind="label",id=1,label=123}}); bad({{kind="label",id=1,label="x\0y"}})
      bad({{kind="label",id=1,label=string.char(255)}})
      bad({{kind="label",id=1,label=string.rep("x",256)}},"too_many")
      bad({{kind="label",id=1,label=string.rep("x",256).."\0"}},"too_many")
      bad(good,nil,"other.main"); bad(good,nil,"widgetsbad..main")
      bad(good,nil,"widgetsbad."..string.rep("x",64)); bad(good,nil,nil,string.rep("x",64))
      bad(good,nil,nil,"x\0y"); bad(good,nil,"widgetsbad.main\0x")
      bad(good,nil,nil,nil,true)
      bad({{kind="label",id=1,label="x",disabled=1}})
      bad({{kind="button",id=1,label="x",text="unused"}})
      bad({{kind="toggle",id=1,label="x",value=1}})
      bad({{kind="slider",id=1,label="x",value=0/0,minimum=0,maximum=1}})
      bad({{kind="slider",id=1,label="x",value=2,minimum=0,maximum=1}})
      bad({{kind="slider",id=1,label="x",value=0,minimum=1,maximum=0}})
      bad({{kind="combo",id=1,label="x",items={"a"},index=1}})
      bad({{kind="list",id=1,label="x",items={"a"},index=-2}})
      bad({{kind="combo",id=1,label="x",items={false}}})
      bad({{kind="combo",id=1,label="x",items={"a",extra="b"}}})
      bad({{kind="table",id=1,items={"a"},columns=2}})
      bad({{kind="table",id=1,items={},columns=17}},"too_many")
      bad({{kind="row",id=1},{kind="label",id=2,parent=3,label="x"}})
      bad({{kind="label",id=1,label="x"},{kind="button",id=1,label="y"}})
      bad({{kind="row",id=1},{kind="label",id=2,parent=1,label="x"},
           {kind="label",id=3,label="x"},{kind="label",id=4,parent=1,label="x"}})
      bad({good[1],extra=good[1]})
      local too={}; for i=1,257 do too[i]={kind="label",id=i,label="x"} end; bad(too,"too_many")
      local items={}; for i=1,257 do items[i]="x" end
      bad({{kind="combo",id=1,label="x",items=items}},"too_many")
      bad({{kind="label",id=1,label="x",typo=true}})
      assert(sco.register_command{name="widgetsbad.status",title="Status",fn=function() return "valid" end})
    )lua");
    Loaded l=Load("widgetsbad");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  widgetsbad: %s\n", l.p->reason.c_str());
    const auto panels=sco::widgets::Panels();
    CHECK(panels.size()==1 && panels[0].title=="Original" && panels[0].nodes[0].label=="Original");
    LuaWidgetRenderer renderer; renderer.events={{1,SCO_WIDGET_BUTTON}};
    CHECK(sco::widgets::RenderPanel("widgetsbad.main",renderer)==Result::Ok);
    CHECK(Logged("[widgetsbad] original callback"));
    Unload(l); sco::widgets::Stop();
}

// Test-only extension: force collection to measure registry ownership, not GC heuristics.
static lua_State* g_widgetState = nullptr;
static int WidgetTestGc(lua_State* L) { lua_gc(L, LUA_GCCOLLECT); return 0; }
static int WidgetTestPauseGc(lua_State* L) { lua_gc(L, LUA_GCSTOP); return 0; }
static void WidgetTestOpen(lua_State* L) {
    g_widgetState = L;
    lua_pushcfunction(L, WidgetTestGc); lua_setfield(L, -2, "widget_test_gc");
    lua_pushcfunction(L, WidgetTestPauseGc); lua_setfield(L, -2, "widget_test_pause_gc");
}
static const sco_lua_extension kWidgetTestExtension = { "widgets-test", WidgetTestOpen, nullptr };

static void TestWidgetsLifetime() {
    CHECK(sco::widgets::Start() == Result::Ok);
    Write("widgetslife", R"lua(
      local w, weak, seen = sco.widgets, setmetatable({}, {__mode="v"}), {}
      local nodes={{kind="button",id=1,label="Click"}}
      local old=function(panel)
        seen[#seen+1]="old"
        assert(w.set_panel(panel,"New",nodes,weak.new))
        sco.widget_test_gc() -- removing registry root must not destroy currently running closure
        seen[#seen+1]="old completed"
      end
      local new=function(panel)
        seen[#seen+1]="new"
        assert(w.remove_panel(panel))
        sco.widget_test_gc()
        seen[#seen+1]="new completed"
      end
      weak.old=old; weak.new=new
      assert(w.set_panel("widgetslife.main","Old",nodes,old))
      old=nil
      local rejected=function() error("rejected callback ran") end; weak.rejected=rejected
      assert(select(2,w.set_panel("widgetslife.main","Invalid",{
        {kind="label",id=1,label="a"},{kind="label",id=1,label="b"}},rejected))=="bad_arg")
      rejected=nil
      sco.widget_test_gc(); assert(weak.old ~= nil and weak.rejected == nil)
      -- Hold the next closure until the reentrant replacement; then drop our last strong reference.
      assert(sco.register_command{name="widgetslife.gc",title="GC",fn=function()
        new=nil; sco.widget_test_gc()
        return tostring(weak.old==nil).." "..tostring(weak.new~=nil) end})
      assert(sco.register_command{name="widgetslife.removed",title="Removed",fn=function()
        sco.widget_test_gc(); return tostring(weak.new==nil).." "..table.concat(seen,",") end})
    )lua");
    Loaded l=Load("widgetslife");
    CHECK(l.p && l.p->state==State::Loaded);
    if (l.p && l.p->state!=State::Loaded) std::printf("  widgetslife: %s\n",l.p->reason.c_str());
    LuaWidgetRenderer renderer; renderer.events={{1,SCO_WIDGET_BUTTON},{1,SCO_WIDGET_BUTTON}};
    CHECK(sco::widgets::RenderPanel("widgetslife.main",renderer)==Result::Ok);
    CHECK(Invoke(g_caller,"widgetslife.gc").text=="true true");
    CHECK(sco::widgets::RenderPanel("widgetslife.main",renderer)==Result::Ok);
    CHECK(Invoke(g_caller,"widgetslife.removed").text=="true old,old completed,new,new completed");
    CHECK(sco::widgets::Panels().empty());
    Unload(l);

    Write("widgetspin", R"lua(
      assert(sco.widgets.set_panel("widgetspin.main","Budget",{{kind="button",id=1,label="Spin"}},
        function() sco.log("info","widget spin entered"); while true do pcall(function() while true do end end) end end))
    )lua");
    Loaded spin=Load("widgetspin");
    CHECK(spin.p && spin.p->state==State::Loaded);
    CHECK(sco::widgets::RenderPanel("widgetspin.main",renderer)==Result::Ok);
    CHECK(!sco_lua_alive(spin.p->self) && Logged("script disabled: ran past its step budget"));
    size_t entered=0; for (const auto& line:g_log) if (line.find("[widgetspin] widget spin entered")!=std::string::npos) ++entered;
    CHECK(entered==1); // queued second event skipped after disabling
    Unload(spin);
    CHECK(sco::widgets::Panels().empty());
    CHECK(sco::widgets::RenderPanel("widgetspin.main",renderer)==Result::NotFound);

    Write("widgetsunload", R"lua(
      assert(sco.widgets.set_panel("widgetsunload.main","Unload",{{kind="button",id=1,label="Click"}},
        function() error("stale unload callback") end))
    )lua");
    Loaded unloaded=Load("widgetsunload");
    CHECK(unloaded.p && unloaded.p->state==State::Loaded);
    class UnloadRenderer final : public sco::widgets::Renderer {
    public:
        Loaded* loaded=nullptr;
        void Render(const sco::widgets::Panel&,std::vector<sco::widgets::Event>& out) override {
            out={{1,SCO_WIDGET_BUTTON},{1,SCO_WIDGET_BUTTON}};
            Unload(*loaded); // invalidate snapshot before event delivery
        }
    } unloadRenderer;
    unloadRenderer.loaded=&unloaded;
    CHECK(sco::widgets::RenderPanel("widgetsunload.main",unloadRenderer)==Result::Crashed);
    CHECK(sco::widgets::Panels().empty() && !Logged("stale unload callback"));
    sco::widgets::Stop();
}

static sco_widget_event_fn g_widgetCallback = nullptr;
static void* g_widgetContext = nullptr;
static bool g_failWidgetRemove = false;
static bool g_collectWidgetSet = false;
static sco_result WidgetCaptureSet(sco_plugin* self,const char* id,const char* title,
    const sco_widget_node* nodes,uint32_t count,uint32_t stride,sco_widget_event_fn fn,void* ctx) {
    if (g_collectWidgetSet) lua_gc(g_widgetState,LUA_GCCOLLECT);
    const auto r=sco::widgets::Table()->set_panel(self,id,title,nodes,count,stride,fn,ctx);
    if (r==SCO_OK) { g_widgetCallback=fn; g_widgetContext=ctx; }
    return r;
}
static sco_result WidgetCaptureRemove(sco_plugin* self,const char* id) {
    return g_failWidgetRemove ? SCO_WRONG_THREAD : sco::widgets::Table()->remove_panel(self,id);
}
static void TestWidgetsCallbackBoundary() {
    CHECK(sco::widgets::Start()==Result::Ok);
    sco_api api=*g_api; api.query_service=WidgetsFixtureQuery;
    sco_widgets_v1 table=*sco::widgets::Table(); table.set_panel=WidgetCaptureSet; table.remove_panel=WidgetCaptureRemove;
    g_widgetsFixture=&table;
    Write("widgetwire",R"lua(
      local weak=setmetatable({},{__mode="v"})
      local fn=function(_,e) assert(e.text=="Good"); sco.log("info","wire good") end
      weak.fn=fn
      assert(sco.widgets.set_panel("widgetwire.main","Boundary",{{kind="text_input",id=1,label="Name",text=""}},fn))
      fn=nil
      assert(sco.register_command{name="widgetwire.remove",title="Remove",fn=function()
        local ok,err=sco.widgets.remove_panel("widgetwire.main")
        sco.widget_test_gc(); return tostring(ok).." "..tostring(err).." "..tostring(weak.fn~=nil) end})
    )lua");
    Loaded l=Load("widgetwire",&api); CHECK(l.p && l.p->state==State::Loaded);
    CHECK(g_widgetCallback && g_widgetContext);
    if (g_widgetCallback) {
        sco_widget_event event{sizeof(sco_widget_event),1,SCO_WIDGET_TEXT_INPUT,0,0,"Good"};
        g_widgetCallback(nullptr,&event,g_widgetContext);
        g_widgetCallback("widgetwire.main",nullptr,g_widgetContext);
        event.size=sizeof(event)-1; g_widgetCallback("widgetwire.main",&event,g_widgetContext);
        void* partial=std::malloc(event.size); std::memcpy(partial,&event,event.size);
        g_widgetCallback("widgetwire.main",static_cast<const sco_widget_event*>(partial),g_widgetContext); std::free(partial);
        event.size=sizeof(event); event.text=nullptr; g_widgetCallback("widgetwire.main",&event,g_widgetContext);
        CHECK(!Logged("wire good") && !Logged("[widgetwire] error: widget event:"));
        g_failWidgetRemove=true;
        CHECK(Invoke(g_caller,"widgetwire.remove").text=="false wrong_thread true");
        event.text="Good"; g_widgetCallback("widgetwire.main",&event,g_widgetContext);
        CHECK(Logged("[widgetwire] wire good"));
        g_failWidgetRemove=false;
        CHECK(Invoke(g_caller,"widgetwire.remove").text=="true nil false");
    }
    Unload(l);
    g_collectWidgetSet=true;
    Write("widgetweak",R"lua(
      sco.widget_test_pause_gc()
      assert(sco.widgets.set_panel("widgetweak.main","Weak source",setmetatable({
        {kind="label",id=1,label=string.rep("q",255)}},{__mode="v"})))
    )lua");
    Loaded weak=Load("widgetweak",&api); CHECK(weak.p && weak.p->state==State::Loaded);
    const auto weakPanels=sco::widgets::Panels();
    CHECK(weakPanels.size()==1 && weakPanels[0].nodes[0].label==std::string(255,'q'));
    Unload(weak); g_collectWidgetSet=false;
    g_widgetCallback=nullptr; g_widgetContext=nullptr; g_widgetsFixture=nullptr; g_failWidgetRemove=false;
    sco::widgets::Stop();
}


static void TestWidgetsTails() {
    CHECK(sco::widgets::Start() == Result::Ok);
    Write("widgetstails", R"lua(
      local w, seen = sco.widgets, {}
      local caps = assert(w.capabilities())
      assert(caps.version == 0x10003 and caps.count == 10 and caps.max_tag == 63)
      assert(w.set_panel("widgetstails.numeric","Numeric",{
        {kind="slider",id=1,label="Darkness",minimum=20,maximum=95,value=74,presentation="integer_percent"},
        {kind="label",id=2,label="Later tag",tag="later"}}))
      assert(select(2,w.set_panel("widgetstails.invalid","Numeric",{
        {kind="slider",id=1,label="Darkness",minimum=20,maximum=95,value=74.5,presentation="integer"}}))=="bad_arg")
      assert(select(2,w.set_panel("widgetstails.invalid","Numeric",{
        {kind="slider",id=1,label="Darkness",minimum=20,maximum=95,value=74,presentation="unknown"}}))=="bad_arg")
      assert(w.remove_panel("widgetstails.numeric"))
      caps.features=0 -- caller owns the copied result
      assert(w.capabilities().features ~= 0)
      local rows={"one","two","three","four"}
      local nodes={
        {kind="column",id=1,tag="root"},
        {kind="list",id=2,parent=1,label="Rows",items=rows,index=-1,
         tooltip="Choose a row",severity=2,first_row=0,visible_rows=2,tag="pick.rows"},
        {kind="button",id=3,parent=1,label="Keep",tag="keep"},
      }
      assert(w.set_panel("widgetstails.main","Tails",nodes,function(_,e)
        if e.kind=="page" then assert(e.node_id==2 and e.row==2);seen[#seen+1]="page"
        elseif e.kind=="list" then assert(e.row==3 and e.index==3);seen[#seen+1]="list"
        else assert(e.kind=="button" and e.row==-1);seen[#seen+1]="button" end
      end))
      -- A conflicting resulting tag, or a malformed subtree, leaves the prior tree.
      assert(select(2,w.update_nodes("widgetstails.main",{{kind="button",id=3,parent=1,label="Bad",tag="pick.rows"}}))=="bad_arg")
      assert(select(2,w.update_nodes("widgetstails.main",{{kind="button",id=3,parent=999,label="Bad"}}))=="bad_arg")
      assert(select(2,w.remove_nodes("widgetstails.main",{999}))=="not_found")
      assert(select(2,w.remove_nodes("widgetstails.main",{1}))=="bad_arg")
      assert(select(2,w.remove_nodes("widgetstails.main",{[1]=3,[3]=2}))=="bad_arg")
      assert(select(2,w.remove_nodes("widgetstails.main",{3, hidden=2}))=="bad_arg")
      assert(select(2,w.remove_nodes("widgetstails.main",{0}))=="bad_arg")
      assert(select(2,w.update_nodes("widgetstails.main",{[1]={kind="label",id=8,label="x"},hidden=true}))=="bad_arg")
      assert(select(2,w.set_panel("widgetstails.invalid","Tails",{{kind="label",id=1,tag="bad tag"}}))=="bad_arg")
      assert(select(2,w.set_panel("widgetstails.invalid","Tails",{{kind="label",id=1,tooltip="a\0b"}}))=="bad_arg")
      assert(select(2,w.set_panel("widgetstails.invalid","Tails",{{kind="label",id=1,severity=4}}))=="bad_arg")
      assert(select(2,w.set_panel("widgetstails.invalid","Tails",{{kind="label",id=1,visible_rows=1}}))=="bad_arg")
      assert(w.update_nodes("widgetstails.main",{{kind="button",id=3,parent=1,label="Changed",tooltip="New",tag="keep"}}))
      assert(w.update_nodes("widgetstails.main",{{kind="label",id=4,parent=1,label="Inserted",tag="new"}}))
      assert(w.remove_nodes("widgetstails.main",{4}))
      assert(sco.register_command{name="widgetstails.seen",title="Seen",fn=function() return table.concat(seen,",") end})
    )lua");
    Loaded live=Load("widgetstails");
    CHECK(live.p && live.p->state==State::Loaded);
    if(live.p && live.p->state!=State::Loaded) std::printf("  widgetstails: %s\n",live.p->reason.c_str());
    const auto panels=sco::widgets::Panels();
    CHECK(panels.size()==1 && panels[0].nodes.size()==3);
    if(panels.size()==1 && panels[0].nodes.size()==3) {
        const auto& n=panels[0].nodes[1];
        CHECK(n.tooltip=="Choose a row" && n.severity==2 && n.visible_rows==2 && n.tag=="pick.rows");
        CHECK(panels[0].nodes[2].label=="Changed" && panels[0].nodes[2].tag=="keep");
    }
    LuaWidgetRenderer renderer;
    renderer.events={{2,SCO_WIDGET_EVENT_PAGE,0,0,"",2},{2,SCO_WIDGET_LIST,3,0,""},{3,SCO_WIDGET_BUTTON,0,0,""}};
    CHECK(sco::widgets::RenderPanel("widgetstails.main",renderer)==Result::Ok);
    CHECK(Invoke(g_caller,"widgetstails.seen").text=="page,list,button");
    Unload(live); CHECK(sco::widgets::Panels().empty());
    sco_api api=*g_api;api.query_service=WidgetsFixtureQuery;
    sco_widgets_v1 table=*sco::widgets::Table();table.capabilities=WidgetFixtureCapabilities;g_widgetsFixture=&table;
    const auto load=[&](const char* source) {
      Write("widgetstailguards",source);Loaded l=Load("widgetstailguards",&api);
      CHECK(l.p && l.p->state==State::Loaded);
      if(l.p && l.p->state!=State::Loaded) std::printf("  widgetstailguards: %s\n",l.p->reason.c_str());
      Unload(l);
    };
    const char* noTail=R"lua(
      local w=sco.widgets
      assert(select(2,w.update_nodes("widgetstailguards.main",{{kind="label",id=1,label="x"}}))=="unavailable")
      assert(select(2,w.remove_nodes("widgetstailguards.main",{1}))=="unavailable")
      assert(select(2,w.set_panel("widgetstailguards.main","x",{{kind="label",id=1,tooltip="tip"}}))=="unavailable")
    )lua";
    g_widgetsMaxVersion=SCO_WIDGETS_VERSION_1_0;g_widgetsVersions.clear();load(noTail);
    CHECK(g_widgetsVersions.size()==12 && g_widgetsVersions[0]==SCO_WIDGETS_VERSION_1_3 && g_widgetsVersions[1]==SCO_WIDGETS_VERSION_1_2 && g_widgetsVersions[2]==SCO_WIDGETS_VERSION_1_1 && g_widgetsVersions[3]==SCO_WIDGETS_VERSION_1_0);
    load(R"lua(
      assert(select(2,sco.widgets.capabilities())=="unavailable")
      assert(sco.widgets.set_panel("widgetstailguards.main","Base",{{kind="label",id=1,label="base"}}))
    )lua");
    g_widgetsMissingResult=SCO_UNAVAILABLE;g_widgetsVersions.clear();load(noTail);
    CHECK(g_widgetsVersions.size()==12 && g_widgetsVersions[3]==SCO_WIDGETS_VERSION_1_0);g_widgetsMissingResult=SCO_NOT_FOUND;
    g_widgetsMaxVersion=SCO_WIDGETS_VERSION_1_1;
    load(R"lua(
      assert(sco.widgets.set_panel("widgetstailguards.main","Tip",{{kind="label",id=1,label="base",tooltip="tip"}}))
      assert(select(2,sco.widgets.set_panel("widgetstailguards.main","Tag",{{kind="label",id=1,label="x",tag="x"}}))=="unavailable")
    )lua");
    g_widgetsMaxVersion=SCO_WIDGETS_VERSION_1_2;g_widgetsFeatureMask=0;load(noTail);
    load(R"lua(assert(select(2,sco.widgets.set_panel("widgetstailguards.main","Numeric",{{kind="slider",id=1,label="x",minimum=0,maximum=1,value=1,presentation="integer"}}))=="unavailable"))lua");
    g_widgetsMaxVersion=SCO_WIDGETS_VERSION_1_3;
    load(R"lua(assert(select(2,sco.widgets.set_panel("widgetstailguards.main","Numeric",{{kind="slider",id=1,label="x",minimum=0,maximum=1,value=1,presentation="integer"}}))=="unavailable"))lua");
    g_widgetsMaxVersion=SCO_WIDGETS_VERSION_1_2;
    load(R"lua(assert(select(2,sco.widgets.set_panel("widgetstailguards.main","Tag",{{kind="label",id=1,tag="x"}}))=="unavailable"))lua");
    g_widgetsFeatureMask=UINT32_MAX;
    g_widgetsFixture=nullptr;g_widgetsVersions.clear();load(R"lua(assert(select(2,sco.widgets.capabilities())=="unavailable"))lua");
    CHECK(g_widgetsVersions.size()==2);g_widgetsFixture=&table;
    g_widgetsQueryError=SCO_CRASHED;g_widgetsVersions.clear();
    load(R"lua(assert(select(2,sco.widgets.capabilities())=="crashed"))lua");
    CHECK(g_widgetsVersions.size()==1 && g_widgetsVersions[0]==SCO_WIDGETS_VERSION_1_3);g_widgetsQueryError=SCO_OK;
    for(uint32_t count:{1u,11u}) {g_widgetsCapsCount=count;load(R"lua(assert(select(2,sco.widgets.capabilities())=="failed"))lua");}g_widgetsCapsCount=0;
    const char* missing=R"lua(
      assert(select(2,sco.widgets.capabilities())=="unavailable")
      assert(select(2,sco.widgets.update_nodes("widgetstailguards.main",{{kind="label",id=1,label="x"}}))=="unavailable")
      assert(select(2,sco.widgets.remove_nodes("widgetstailguards.main",{1}))=="unavailable")
    )lua";
    for(size_t length:{offsetof(sco_widgets_v1,update_nodes)+sizeof(table.update_nodes)-1,offsetof(sco_widgets_v1,remove_nodes)+sizeof(table.remove_nodes)-1,offsetof(sco_widgets_v1,capabilities)+sizeof(table.capabilities)-1}) {
      table.size=static_cast<uint32_t>(length);void* shortTable=std::malloc(length);std::memcpy(shortTable,&table,length);g_widgetsFixture=static_cast<sco_widgets_v1*>(shortTable);
      load(missing);std::free(shortTable);
    }
    table=*sco::widgets::Table();table.capabilities=nullptr;table.update_nodes=nullptr;table.remove_nodes=nullptr;g_widgetsFixture=&table;load(missing);
    g_widgetsFixture=nullptr;sco::widgets::Stop();
}

static void TestWidgetsStaleCookies() {
    CHECK(sco::widgets::Start()==Result::Ok);
    sco_api api=*g_api;api.query_service=WidgetsFixtureQuery;
    sco_widgets_v1 table=*sco::widgets::Table();table.set_panel=WidgetCaptureSet;g_widgetsFixture=&table;
    Write("widgetcookie",R"lua(
      local w=sco.widgets
      assert(w.set_panel("widgetcookie.main","Old",{{kind="button",id=1,label="Click"}},function() sco.log("info","cookie old") end))
      assert(sco.register_command{name="widgetcookie.replace",title="Replace",fn=function()
        assert(w.set_panel("widgetcookie.main","New",{{kind="button",id=1,label="Click"}},function() sco.log("info","cookie new") end));return "replaced" end})
    )lua");
    Loaded live=Load("widgetcookie",&api);CHECK(live.p&&live.p->state==State::Loaded);
    auto oldFn=g_widgetCallback;auto oldCtx=g_widgetContext;
    CHECK(Invoke(g_caller,"widgetcookie.replace").text=="replaced");
    sco_widget_event event{sizeof(event),1,SCO_WIDGET_BUTTON,0,0,nullptr};
    CHECK(oldFn && oldCtx && oldCtx!=g_widgetContext);
    oldFn("widgetcookie.main",&event,oldCtx);CHECK(!Logged("cookie old")&&!Logged("cookie new"));
    g_widgetCallback("widgetcookie.main",&event,g_widgetContext);CHECK(Logged("[widgetcookie] cookie new"));
    Unload(live);g_log.clear();
    Loaded reload=Load("widgetcookie",&api);CHECK(reload.p&&reload.p->state==State::Loaded);
    oldFn("widgetcookie.main",&event,oldCtx);CHECK(!Logged("cookie old"));
    sco_widget_event_1_1 extended{{sizeof(extended),1,SCO_WIDGET_BUTTON,0,0,nullptr},-1,0};
    extended.base.size=sizeof(extended)-1;void* partial=std::malloc(extended.base.size);std::memcpy(partial,&extended,extended.base.size);
    g_widgetCallback("widgetcookie.main",static_cast<sco_widget_event*>(partial),g_widgetContext);std::free(partial);CHECK(!Logged("cookie old"));
    extended.base.size=sizeof(extended);extended.row=0;g_widgetCallback("widgetcookie.main",&extended.base,g_widgetContext);CHECK(!Logged("cookie old"));
    extended.row=-1;g_widgetCallback("widgetcookie.main",&extended.base,g_widgetContext);CHECK(Logged("[widgetcookie] cookie old"));
    Unload(reload);g_widgetsFixture=nullptr;g_widgetCallback=nullptr;g_widgetContext=nullptr;sco::widgets::Stop();
}
static void TestWidgetsBounds() {
    CHECK(sco::widgets::Start()==Result::Ok);
    Write("widgetbounds",R"lua(
      local nodes={}; for i=1,256 do nodes[i]={kind="label",id=i,label=string.rep("x",255)} end
      for p=1,16 do assert(sco.widgets.set_panel("widgetbounds.p"..p,"Bounds",nodes)) end
      assert(select(2,sco.widgets.set_panel("widgetbounds.extra","Bounds",{{kind="label",id=1,label="x"}}))=="too_many")
      for p=1,16 do assert(sco.widgets.remove_panel("widgetbounds.p"..p)) end
      local items={}; for i=1,256 do items[i]="x" end
      local id="widgetbounds."..string.rep("x",50)
      assert(#id==63)
      assert(sco.widgets.set_panel(id,string.rep("t",63),{{kind="table",id=1,items=items,columns=16}}))
      assert(sco.widgets.remove_panel(id))
      for p=1,64 do assert(sco.widgets.set_panel("widgetbounds.p"..p,"Bounds",{{kind="button",id=1,label="x"}},function() end)) end
      assert(select(2,sco.widgets.set_panel("widgetbounds.extra","Bounds",{{kind="button",id=1,label="x"}},function() end))=="too_many")
      assert(sco.widgets.set_panel("widgetbounds.p1","Replace at capacity",{{kind="label",id=1,label="x"}}))
      assert(sco.widgets.remove_panel("widgetbounds.p1"))
      assert(sco.widgets.set_panel("widgetbounds.extra","Reused slot",{{kind="button",id=1,label="x"}},function() end))
    )lua");
    Loaded l=Load("widgetbounds"); CHECK(l.p && l.p->state==State::Loaded);
    if(l.p && l.p->state!=State::Loaded) std::printf("  widgetbounds: %s\n",l.p->reason.c_str());
    CHECK(sco::widgets::Panels().size()==64);
    Unload(l); CHECK(sco::widgets::Panels().empty()); sco::widgets::Stop();
}

static void TestWidgetsExample(const fs::path& sdk) {
    fs::copy(sdk / "examples/widgets_lua",g_root / "widgets_lua",fs::copy_options::recursive);
    Loaded absent=Load("widgets_lua"); CHECK(absent.p && absent.p->state==State::Loaded);
    CHECK(Invoke(g_caller,"widgets_lua.state").text=="0 events"); Unload(absent);
    CHECK(sco::widgets::Start()==Result::Ok);
    Loaded l=Load("widgets_lua"); CHECK(l.p && l.p->state==State::Loaded);
    const auto panels=sco::widgets::Panels(); CHECK(panels.size()==1 && panels[0].nodes.size()==10);
    LuaWidgetRenderer renderer;
    renderer.events={{4,SCO_WIDGET_BUTTON},{5,SCO_WIDGET_TOGGLE,0,1},{6,SCO_WIDGET_SLIDER,0,0.75},
      {7,SCO_WIDGET_COMBO,1},{8,SCO_WIDGET_LIST,0},{9,SCO_WIDGET_TEXT_INPUT,0,0,"Edited"}};
    CHECK(sco::widgets::RenderPanel("widgets_lua.main",renderer)==Result::Ok);
    CHECK(Invoke(g_caller,"widgets_lua.state").text=="6 events");
    Unload(l); CHECK(sco::widgets::Panels().empty()); sco::widgets::Stop();
}

static void TestGreeterExample(const fs::path& sdk) {
    fs::copy(sdk / "examples/greeter", g_root / "greeter", fs::copy_options::recursive);
    Loaded l = Load("greeter");
    CHECK(l.p && l.p->state == State::Loaded);
    CHECK(Logged("[plugin] loaded greeter 1.0.0 (lua) from greeter/main.lua"));
    CHECK(HasCommand("greeter.greet"));
    Reply r = Invoke(g_caller, "greeter.greet", { Str("Pilot One"), Bool(true) });
    CHECK(r.called && r.r == SCO_OK && r.text == "HI, PILOT ONE!");
    r = Invoke(g_caller, "greeter.greet", { Str("Pilot Two"), Bool(false) });
    CHECK(r.r == SCO_OK && r.text == "Hi, Pilot Two");
    r = Invoke(g_caller, "greeter.greet", { Str("x") });                     // wrong arg count
    CHECK(r.r == SCO_BAD_ARG);
    char status[256];
    CHECK(sco::GetStatus(status, sizeof(status)) && strstr(status, "greeter: Greeter ready"));

    sco::Dispatch("game.ready", nullptr);
    CHECK(Logged("[greeter] warning: teleport is not available on this game build"));
    sco::caps::Set("teleport", true);
    sco::Dispatch("game.ready", nullptr);
    CHECK(Logged("[greeter] teleport is available"));
    sco::Dispatch("game.exit", nullptr);
    CHECK(Logged("[greeter] greeted 2 times"));

    Unload(l);
    CHECK(l.p->state == State::Unloaded && !HasCommand("greeter.greet"));
    CHECK(Invoke(g_caller, "greeter.greet", { Str("x"), Bool(false) }).r == SCO_NOT_FOUND);
}

// ---- the sco table ----------------------------------------------------------------------------

static void TestTable() {
    Write("table", R"(
        local ticks, last, tasks = 0, nil, 0
        local function on_tick(event, ms)
          ticks = ticks + 1; last = ms
          if ticks == 2 then sco.unsubscribe("tick", on_tick) end
        end
        assert(sco.subscribe("tick", on_tick))
        local ok, err = sco.subscribe("tick", on_tick)
        assert(not ok and err == "bad_arg", "same key twice")
        for i = 1, 64 do assert(sco.subscribe("game.exit", function() return i end)) end
        local full, why = sco.subscribe("game.exit", function() end)
        assert(not full and why == "too_many", "65th function on one event: " .. tostring(why))
        assert(sco.api_major == 1 and sco.api_minor == 1)
        assert(sco.host_version() == "sco-lua test 1.0")
        assert(sco.run_on_game_thread(function() tasks = tasks + 1 end))
        assert(select(2, sco.register_command{ name = "other.x", title = "X", fn = print }) == "bad_arg")
        assert(select(2, sco.register_command{ name = "table.y", title = "Y", fn = print,
                                               args = {{ name = "a", type = "table" }} }) == "bad_arg")
        sco.register_command{ name = "table.state", title = "State",
          fn = function() return string.format("ticks=%d last=%s tasks=%d", ticks, tostring(last), tasks) end }
        sco.register_command{ name = "table.add", title = "Add",
          args = {{ name = "a", type = "int" }, { name = "b", type = "float", help = "second" }},
          fn = function(a, b) return tostring(a + b) end }
        sco.register_command{ name = "table.call", title = "Call",
          fn = function()
            local ok, reply = sco.invoke("table.add", 2, 0.5)
            local bad, why = sco.invoke("table.add", "2", 0.5)
            local none, nf = sco.invoke("nope.nope")
            local float, fw = sco.invoke("table.add", 2.0, 0.5)              -- int takes no float
            return table.concat({ tostring(ok), reply, tostring(bad), why, tostring(none), nf, tostring(float), fw }, " ")
          end }
        sco.register_command{ name = "table.list", title = "List",
          fn = function()
            for _, c in ipairs(sco.list_commands()) do
              if c.name == "table.add" then return c.title .. " " .. #c.args .. " " .. c.args[2].type .. " " .. c.args[2].help end
            end
          end }
        sco.register_command{ name = "table.print", title = "Print", fn = function() print("a", 1, nil) end }
    )");
    Loaded l = Load("table");
    CHECK(l.p && l.p->state == State::Loaded);
    CHECK(Invoke(g_caller, "table.state").text == "ticks=0 last=nil tasks=0");
    sco::GameThreadTick(1000);
    CHECK(Invoke(g_caller, "table.state").text == "ticks=1 last=1000 tasks=1");
    sco::GameThreadTick(1100);
    sco::GameThreadTick(1200);                                                // unsubscribed after 2
    CHECK(Invoke(g_caller, "table.state").text == "ticks=2 last=1100 tasks=1");
    CHECK(Invoke(g_caller, "table.add", { Int(40), sco_arg{ SCO_ARG_FLOAT, 0, { .f = 2.5 } } }).text == "42.5");
    CHECK(Invoke(g_caller, "table.call").text == "true 2.5 false bad_arg false not_found false bad_arg");
    CHECK(Invoke(g_caller, "table.list").text == "Add 2 float second");
    CHECK(Invoke(g_caller, "table.print").r == SCO_OK && Logged("[table] a\t1\tnil"));
    Unload(l);
}

// ---- sco.ui hotkeys ---------------------------------------------------------------------------

static void TestHotkeys() {
    CHECK(sco::ui::Start() == Result::Ok);
    CHECK(sco::ui::ReserveChord("f6") == Result::Ok);
    Write("keys", R"(
        sco.register_command{ name = "keys.add", title = "Add",
          args = {{ name = "a", type = "int" }, { name = "b", type = "float" }},
          fn = function(a, b) return tostring(a + b) end }
        assert(sco.bind_hotkey("Ctrl+Alt+K", "keys.add", 40, 2))                 -- 2 is a float: keys.add says so
        assert(sco.bind_hotkey("ctrl+alt+l", "later.cmd", "x", true, 1.5))      -- not registered: Lua types
        local ok, why, msg = sco.bind_hotkey("f6", "keys.add", 1, 2)
        assert(not ok and why == "bad_arg" and msg == "f6 is reserved by the host", tostring(msg))
        ok, why, msg = sco.bind_hotkey("alt+ctrl+k", "keys.add", 1, 2)
        assert(not ok and msg == "ctrl+alt+k is bound by 'keys' to keys.add", tostring(msg))
        assert(select(2, sco.bind_hotkey("ctrl+alt+m", "keys.add", 1)) == "bad_arg")        -- arg count
        assert(select(2, sco.bind_hotkey("ctrl+alt+m", "keys.add", "1", 2)) == "bad_arg")  -- arg type
        assert(select(2, sco.bind_hotkey("ctrl+alt+m", "later.cmd", {})) == "bad_arg")    -- a table
        assert(select(2, sco.bind_hotkey("ctrl++", "keys.add", 1, 2)) == "bad_arg")
        assert(select(2, sco.unbind_hotkey("f12")) == "not_found")
        assert(sco.bind_hotkey("f12", "keys.add", 1, 2) and sco.unbind_hotkey("F12"))
    )");
    Loaded l = Load("keys");
    CHECK(l.p && l.p->state == State::Loaded);
    std::string reply;
    CHECK(sco::ui::Dispatch("alt+ctrl+k", &reply) == Result::Ok && reply == "42.0");
    const auto keys = sco::ui::Hotkeys();
    CHECK(keys.size() == 2 && keys[0].chord == "ctrl+alt+k" && keys[0].owner == "keys" && keys[1].nargs == 3);
    Unload(l);
    CHECK(sco::ui::Hotkeys().empty());   // withdrawn with the script
    sco::ui::Stop();
}

// ---- the sandbox ------------------------------------------------------------------------------

static void TestSandbox() {
    Write("box", R"(
        local gone = {}
        for _, n in ipairs{ "io", "os", "package", "require", "debug", "dofile", "loadfile", "load",
                            "loadstring", "collectgarbage", "coroutine", "module" } do
          if _G[n] ~= nil then gone[#gone + 1] = n end
        end
        assert(#gone == 0, "present: " .. table.concat(gone, ","))
        assert(string.dump == nil)
        assert(string and table and math and utf8)
        local ok, err = pcall(setmetatable, {}, { __gc = function() end })
        assert(not ok and err:find("__gc is not allowed"), tostring(err))
        assert(getmetatable(setmetatable({}, { __index = {} })) ~= nil)
    )");
    Loaded l = Load("box");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  box: %s\n", l.p->reason.c_str());
    Unload(l);

    // Precompiled chunks are refused: the loader passes mode "t".
    Write("bytecode", std::string("\x1bLua\x54\x00", 6) + "junk");
    l = Load("bytecode");
    CHECK(l.p && l.p->state == State::Refused && l.p->reason.find("binary") != std::string::npos);

    Write("syntax", "local x = = 1\n");
    l = Load("syntax");
    CHECK(l.p && l.p->state == State::Refused && l.p->reason.find("main.lua:1:") != std::string::npos);

    // A script that fails at load leaves nothing registered.
    Write("halfway", "sco.register_command{ name = 'halfway.a', title = 'A', fn = print }\n"
                     "sco.subscribe('tick', print)\nerror('stop here')\n");
    const size_t subs = sco::SubscriptionCount();
    l = Load("halfway");
    CHECK(l.p && l.p->state == State::Refused && l.p->reason.find("stop here") != std::string::npos);
    CHECK(!HasCommand("halfway.a") && sco::SubscriptionCount() == subs);

    // Disabled during load (three errors in its own command, reached through sco.invoke): the
    // load chunk keeps running, but sco.* mutators answer unavailable and the load fails.
    Write("selfkill", R"(
        sco.register_command{ name = "selfkill.boom", title = "Boom", fn = function() error("boom") end }
        for _ = 1, 3 do sco.invoke("selfkill.boom") end
        local _, a = sco.subscribe("tick", print)
        local _, b = sco.run_on_game_thread(print)
        local _, c = sco.register_command{ name = "selfkill.more", title = "More", fn = print }
        sco.log("info", table.concat({ tostring(a), tostring(b), tostring(c) }, " "))
    )");
    l = Load("selfkill");
    CHECK(l.p && l.p->state == State::Refused && l.p->reason.find("disabled") != std::string::npos);
    if (l.p && l.p->state != State::Refused) std::printf("  selfkill: loaded\n");
    CHECK(Logged("[selfkill] unavailable unavailable unavailable"));
    CHECK(!HasCommand("selfkill.more") && sco::SubscriptionCount() == subs);
}

// ---- limits -----------------------------------------------------------------------------------

static void TestLimits() {
    // Load itself runs past the budget.
    Write("spin", "while true do end\n");
    Loaded l = Load("spin");
    CHECK(l.p && l.p->state == State::Refused && l.p->reason.find("step budget") != std::string::npos);

    Write("limits", R"(
        local n = 0
        local function big(len, c) local s = c or "x" while #s < len do s = s .. s end return s:sub(1, len) end
        sco.register_command{ name = "limits.spin", title = "Spin", fn = function() while true do end end }
        sco.register_command{ name = "limits.shield", title = "Shield",
          fn = function() while true do pcall(function() while true do end end) end end }
        sco.register_command{ name = "limits.rep", title = "Rep",
          fn = function() return tostring(#string.rep("x", 5000000)) end }
        sco.register_command{ name = "limits.find", title = "Find",
          fn = function() local s = string.rep(string.rep("a", 1000), 2000) return tostring(s:find(string.rep("a", 30) .. "b", 1, true)) end }
        -- One library call, many values: charged by size, not 1 step per call.
        sco.register_command{ name = "limits.unpack", title = "Unpack", fn = function()
          local t = table.pack(big(500000):byte(1, -1))
          for _ = 1, 1000 do table.unpack(t, 1, t.n) end
          return "unpacked" end }
        sco.register_command{ name = "limits.byte", title = "Byte", fn = function()
          local s = big(500000)
          for _ = 1, 1000 do s:byte(1, -1) end
          return "bytes" end }
        sco.register_command{ name = "limits.needle", title = "Needle", fn = function()
          local s = big(4194304)
          return tostring(s:find(s:sub(1, 2097152) .. "y", 1, true)) end }
        sco.register_command{ name = "limits.class", title = "Class", fn = function()
          return tostring(big(1000000):find("[" .. big(1048576, "y") .. "x]*")) end }
        sco.register_command{ name = "limits.sort", title = "Sort",
          fn = function() local t = {} for i = 1, 60000 do t[i] = -i end table.sort(t) return "sorted" end }
        sco.register_command{ name = "limits.ok", title = "OK",
          fn = function() local s = 0 for i = 1, 100000 do s = s + i end return tostring(s) end }
        sco.register_command{ name = "limits.count", title = "Count", fn = function() n = n + 1 return tostring(n) end }
    )");
    // Each budget case gets a fresh copy of the script: an overrun disables it.
    struct Case { const char* cmd; };
    for (const char* cmd : { "limits.spin", "limits.shield", "limits.rep", "limits.find", "limits.sort",
                             "limits.unpack", "limits.byte", "limits.needle", "limits.class" }) {
        l = Load("limits");
        CHECK(l.p && l.p->state == State::Loaded);
        CHECK(Invoke(g_caller, "limits.ok").text == "5000050000");             // fits the budget
        const Reply r = Invoke(g_caller, cmd);
        CHECK(r.r == SCO_CRASHED && r.text.find("step budget") != std::string::npos);
        if (r.r != SCO_CRASHED) std::printf("  %s -> %d %s\n", cmd, r.r, r.text.c_str());
        CHECK(!sco_lua_alive(l.p->self));
        CHECK(Invoke(g_caller, "limits.count").r == SCO_UNAVAILABLE);         // disabled
        CHECK(Logged("[limits] error: script disabled: ran past its step budget"));
        Unload(l);
    }

    // One budget per outermost entry: a script can't multiply it by calling another script's
    // command in a loop (each call alone fits the budget).
    Write("worker", R"(
        sco.register_command{ name = "worker.chunk", title = "Chunk",
          fn = function() local s = 0 for i = 1, 150000 do s = s + i end return "done" end }
    )");
    Write("boss", R"(
        sco.register_command{ name = "boss.run", title = "Run", fn = function()
          for i = 1, 100 do
            local ok, why = sco.invoke("worker.chunk")
            if not ok then return "stopped at " .. i .. ": " .. tostring(why) end
          end
          return "ran all 100"
        end }
    )");
    {
        Loaded w = Load("worker");
        Loaded bo = Load("boss");
        CHECK(Invoke(g_caller, "worker.chunk").text == "done");                // alone: fits
        const Reply r = Invoke(g_caller, "boss.run");
        CHECK(r.r == SCO_CRASHED && r.text.find("step budget") != std::string::npos);
        if (r.r != SCO_CRASHED) std::printf("  boss.run -> %d %s\n", r.r, r.text.c_str());
        CHECK(!sco_lua_alive(bo.p->self));
        Unload(bo);
        Unload(w);
    }

    // Memory: the 64 MiB cap fails the allocation; the script is disabled.
    Write("hog", R"(
        sco.register_command{ name = "hog.eat", title = "Eat", fn = function()
          local mb = string.rep(string.rep("x", 1000), 1000) local t = {} for i = 1, 100 do t[i] = mb .. i end end }
        sco.register_command{ name = "hog.ok", title = "OK", fn = function() return "fine" end }
    )");
    l = Load("hog");
    CHECK(Invoke(g_caller, "hog.ok").text == "fine");
    const Reply r = Invoke(g_caller, "hog.eat");
    CHECK(r.r == SCO_TOO_MANY && r.text.find("not enough memory") != std::string::npos);
    CHECK(!sco_lua_alive(l.p->self) && Logged("[hog] error: script disabled: out of memory"));
    Unload(l);

    // A non-string error object raised with the heap at the cap: turning it into text must not
    // allocate outside protected mode ("42" would need a new string, the allocation fails with no
    // handler, and Lua aborts the process). fill() pins memory to the last few bytes, inside
    // pcall, then the callback raises 42.
    static const char* const kFill = R"(
        local slots, n = {}, 0
        for i = 1, 1024 do slots[i] = false end
        local big = "x"
        for _ = 1, 20 do big = big .. big end
        local bytes = {}
        for i = 128, 191 do bytes[#bytes + 1] = string.char(i) end
        local size, a, b, filled = 0, 1, 0, false
        local function long() n = n + 1; slots[n] = big:sub(1, size) end
        local function short()
          b = b + 1; if b > #bytes then a, b = a + 1, 1 end
          n = n + 1; slots[n] = bytes[a] .. bytes[b]
        end
        local function fill()
          if filled then return end
          filled = true
          for _, s in ipairs{ 1048576, 65536, 4096, 256, 48 } do
            size = s
            while pcall(long) do end
          end
          while pcall(short) do end
        end
    )";
    Write("full", std::string(kFill) +
          "sco.register_command{ name = 'full.boom', title = 'Boom', fn = function() fill() error(42) end }\n");
    l = Load("full");
    CHECK(l.p && l.p->state == State::Loaded);
    const Reply full = Invoke(g_caller, "full.boom");
    CHECK(full.r == SCO_BAD_ARG && full.text == "42");
    if (full.text != "42") std::printf("  full.boom -> %d %s\n", full.r, full.text.c_str());
    CHECK(Logged("[full] error: full.boom: 42"));
    Invoke(g_caller, "full.boom");
    Invoke(g_caller, "full.boom");
    CHECK(!sco_lua_alive(l.p->self));                                        // 3 errors (or out of memory)
    Unload(l);
    Write("fullevt", std::string(kFill) + "sco.subscribe('game.ready', function() fill() error(42) end)\n");
    l = Load("fullevt");
    CHECK(l.p && l.p->state == State::Loaded);
    sco::Dispatch("game.ready", nullptr);
    CHECK(Logged("[fullevt] error: event game.ready: 42"));
    Unload(l);

    // Three errors in callbacks disable the script; the first two only log.
    Write("flaky", R"(
        sco.register_command{ name = "flaky.boom", title = "Boom", fn = function() error("boom") end }
        sco.register_command{ name = "flaky.ok", title = "OK", fn = function() return "ok" end }
        sco.register_command{ name = "flaky.badreply", title = "Bad", fn = function() return {} end }
    )");
    l = Load("flaky");
    Reply b = Invoke(g_caller, "flaky.boom");
    CHECK(b.r == SCO_BAD_ARG && b.text.find("main.lua:2: boom") != std::string::npos);
    CHECK(Invoke(g_caller, "flaky.badreply").text.find("reply must be a string or nil, not table") != std::string::npos);
    CHECK(Invoke(g_caller, "flaky.ok").text == "ok");
    CHECK(Invoke(g_caller, "flaky.boom").r == SCO_BAD_ARG);
    CHECK(Invoke(g_caller, "flaky.ok").r == SCO_UNAVAILABLE && Logged("[flaky] error: script disabled: 3 errors"));
    Unload(l);

    // An event callback error is logged with the event name.
    Write("evt", "sco.subscribe('game.ready', function() error('nope') end)\n");
    l = Load("evt");
    sco::Dispatch("game.ready", nullptr);
    CHECK(Logged("[evt] error: event game.ready: main.lua:1: nope"));
    Unload(l);

    // Each task gets a fresh budget, so a script may have at most 16 waiting: a task that queues
    // two more each tick levels off at 16 instead of filling the host's 256-slot queue.
    Write("fan", R"(
        local queued, most, refused = 0, 0, 0
        local function task()
          queued = queued - 1
          for _ = 1, 2 do
            local ok, why = sco.run_on_game_thread(task)
            if ok then queued = queued + 1; most = math.max(most, queued)
            elseif why == "too_many" then refused = refused + 1 end
          end
        end
        assert(sco.run_on_game_thread(task))
        queued, most = 1, 1
        sco.register_command{ name = "fan.state", title = "State",
          fn = function() return string.format("most=%d refused=%s", most, tostring(refused > 0)) end }
    )");
    l = Load("fan");
    CHECK(l.p && l.p->state == State::Loaded);
    for (uint32_t t = 1; t <= 10; ++t) sco::GameThreadTick(t * 100);
    const Reply fan = Invoke(g_caller, "fan.state");
    CHECK(fan.text == "most=16 refused=true");
    if (fan.text != "most=16 refused=true") std::printf("  fan.state -> %s\n", fan.text.c_str());
    CHECK(sco_lua_alive(l.p->self));
    Unload(l);
}

// Memory, every script together: SCO_LUA_TOTAL_MEMORY_LIMIT (256 MiB) caps the sum, so scripts each
// under their own 64 MiB can't take the game's memory between them. Each pool<n>.hold keeps one more
// 8 MiB string, a 1 KiB chunk repeated 8192 times (string.rep costs a step per copy, so about 9k
// steps; string.rep("x", 8 MiB) would be 8M steps and run out of budget first), and catches a failed allocation with pcall so the script stays alive. 10
// scripts x 4 calls ask for 320 MiB: between 24 and 32 calls fit (192 to 256 MiB, less while a copy
// is in flight), and once one is refused every later one is too.
static void TestTotalMemory() {
    std::vector<Loaded> pools;
    pools.reserve(10);
    for (int i = 1; i <= 10; ++i) {
        const std::string id = "pool" + std::to_string(i);
        Write(id, "local kept, chunk = {}, string.rep(\"x\", 1024)\nsco.register_command{ name = \"" + id + ".hold\", title = \"Hold\",\n"
                  "  fn = function()\n"
                  "    local ok = pcall(function() kept[#kept + 1] = string.rep(chunk, 8192) end)\n"
                  "    return ok and \"held\" or \"refused\" end }\n");
        pools.push_back(Load(id));
        CHECK(pools.back().p && pools.back().p->state == State::Loaded);
    }
    int held = 0, refused = 0, other = 0;
    bool heldAfterRefused = false;
    for (int i = 1; i <= 10; ++i)
        for (int k = 0; k < 4; ++k) {
            const std::string cmd = "pool" + std::to_string(i) + ".hold";
            const Reply r = Invoke(g_caller, cmd.c_str());
            if (r.text == "held") { ++held; if (refused) heldAfterRefused = true; }
            else if (r.text == "refused") ++refused;
            else { ++other; std::printf("  %s -> %s\n", cmd.c_str(), r.text.c_str()); }
        }
    CHECK(other == 0 && held + refused == 40 && !heldAfterRefused);
    CHECK(held >= 24 && held <= 32);
    if (!(held >= 24 && held <= 32)) std::printf("  pools: %d held, %d refused\n", held, refused);
    for (const auto& l : pools) CHECK(l.p && sco_lua_alive(l.p->self));
    for (auto& l : pools) Unload(l);
    // Unloading gives the memory back.
    Write("pool11", "local kept, chunk = nil, string.rep(\"x\", 1024)\nsco.register_command{ name = \"pool11.hold\", title = \"Hold\",\n"
                    "  fn = function() kept = string.rep(chunk, 8192) return \"held\" end }\n");
    Loaded l = Load("pool11");
    CHECK(Invoke(g_caller, "pool11.hold").text == "held");
    Unload(l);
}

// ---- the extension registry (sco_lua_ext.h) ---------------------------------------------------

static int g_extOpened = 0;
static void ExtOpen(lua_State* L) {
    ++g_extOpened;
    lua_pushstring(L, "from an extension");
    lua_setfield(L, -2, "ext_probe");
}
static int ExtEvent(lua_State* L, const char* event, const void*) {
    if (std::strcmp(event, "probe.ping") != 0) return 0;
    lua_pushstring(L, "probe payload");
    return 1;
}
static const sco_lua_extension kProbeExt = { "probe", ExtOpen, ExtEvent };

static void TestExtensions() {
    CHECK(sco_lua_add_extension(nullptr) == SCO_BAD_ARG);
    const sco_lua_extension noOpen = { "bad", nullptr, nullptr };
    CHECK(sco_lua_add_extension(&noOpen) == SCO_BAD_ARG);
    // A script loaded before the extension doesn't see it.
    Write("extbefore", "assert(sco.ext_probe == nil)\n");
    Loaded before = Load("extbefore");
    CHECK(before.p && before.p->state == State::Loaded);
    CHECK(sco_lua_add_extension(&kProbeExt) == SCO_OK);
    CHECK(sco_lua_add_extension(&kProbeExt) == SCO_BAD_ARG);   // once only
    Write("extafter", R"(
        assert(sco.ext_probe == "from an extension")
        sco.subscribe("probe.ping", function(name, data) sco.log("info", "ping " .. tostring(data)) end)
        sco.subscribe("probe.other", function(name, data) sco.log("info", "other " .. tostring(data)) end)
    )");
    const int opened = g_extOpened;
    Loaded after = Load("extafter");
    CHECK(after.p && after.p->state == State::Loaded && g_extOpened == opened + 1);
    sco::Dispatch("probe.ping", nullptr);
    CHECK(Logged("[extafter] ping probe payload"));      // the extension's value
    sco::Dispatch("probe.other", nullptr);
    CHECK(Logged("[extafter] other nil"));               // no extension knows it: nil
    Unload(after);
    Unload(before);
}

#ifndef SCO_KERNEL_ONLY   // the Star Citizen game pack (SCO_GAME_SC)
#include "game_contracts/lua_cases.inc"
#endif   // SCO_KERNEL_ONLY

// ---- sco.store (over the host service sco.storage) ---------------------------------------------

// Opens and closes SCO_STORAGE_MAX_CURSORS cursors as `self`: true when none was refused, so the
// script left no cursor open.
static bool AllCursorsFree(sco_plugin* self) {
    const sco_storage_v1* st = sco::storage::Table();
    std::vector<uint64_t> ids;
    bool ok = true;
    for (uint32_t i = 0; i < SCO_STORAGE_MAX_CURSORS; ++i) {
        uint64_t id = 0;
        if (st->query(self, "SELECT 1", nullptr, 0, &id) != SCO_OK) { ok = false; break; }
        ids.push_back(id);
    }
    for (uint64_t id : ids) st->close(self, id);
    return ok;
}

// ---- script-only administrative reload -----------------------------------------------------

static void TestReloadLifecycle() {
    const fs::path data = g_root.parent_path() / "reload-storage";
    fs::remove_all(data);
    sco::storage::Options options;
    options.dataRoot = data;
    CHECK(sco::storage::Start(options) == Result::Ok);
    CHECK(sco::ui::Start() == Result::Ok);
    const size_t subscriptions = sco::SubscriptionCount();
    Write("reloadlife", R"lua(
        assert(sco.game == nil and sco.datacore == nil and sco.provide_service == nil)
        assert(sco.store.get("pending") == nil, "uncommitted data survived")
        local generation = tonumber(sco.store.get("generation") or "0") + 1
        assert(sco.store.put("generation", tostring(generation)))
        assert(sco.store.begin() and sco.store.put("pending", "discard me"))
        local ticks = 0
        assert(sco.subscribe("tick", function()
          ticks = ticks + 1; sco.log("info", "tick generation " .. generation) end))
        assert(sco.register_command{ name = "reloadlife.state", title = "State",
          fn = function() return generation .. " " .. ticks end })
        assert(sco.register_command{ name = "reloadlife.only" .. generation, title = "Generation", fn = print })
        assert(sco.bind_hotkey("ctrl+alt+r", "reloadlife.state"))
        for _ = 1, 16 do
          assert(sco.run_on_game_thread(function() sco.log("info", "pending generation " .. generation) end))
        end
        assert(select(2, sco.run_on_game_thread(print)) == "too_many")
    )lua");
    Loaded l = Load("reloadlife");
    CHECK(l.p && l.p->state == State::Loaded);
    for (int generation = 1; generation <= 3; ++generation) {
        CHECK(Invoke(g_caller, "reloadlife.state").text == std::to_string(generation) + " 0");
        CHECK(sco::SubscriptionCount() == subscriptions + 1);
        CHECK(sco::ui::Hotkeys().size() == 1);
        CHECK(AllCursorsFree(l.p->self));
        if (generation < 3) {
            CHECK(ReloadScript(l));
            const std::string old = "reloadlife.only" + std::to_string(generation);
            CHECK(!HasCommand(old.c_str()));
            CHECK(Invoke(g_caller, old.c_str()).r == SCO_NOT_FOUND);
        }
    }
    sco_plugin* finalOwner = l.p->self;
    P::UnloadOne(*l.p, P::PlatformModuleOps(), &kLua);
    CHECK(l.p->state == State::Unloaded && !sco_lua_alive(finalOwner));
    CHECK(sco::SubscriptionCount() == subscriptions && sco::ui::Hotkeys().empty());
    CHECK(!HasCommand("reloadlife.state") && !HasCommand("reloadlife.only3"));
    // Drain after every old Lua state is gone: stale task/event contexts must never execute.
    sco::GameThreadTick(4200);
    CHECK(!Logged("[reloadlife] pending generation") && !Logged("[reloadlife] tick generation"));
    CHECK(ReloadScript(l));   // unload is a no-op for the already-unloaded discovery record
    CHECK(Invoke(g_caller, "reloadlife.state").text == "4 0");
    sco::GameThreadTick(4300);
    CHECK(Invoke(g_caller, "reloadlife.state").text == "4 1");
    CHECK(Logged("[reloadlife] pending generation 4") && Logged("[reloadlife] tick generation 4"));
    Unload(l);
    sco::ui::Stop();
    sco::storage::Stop();
}

static void TestReloadBudgets() {
    Write("reloadbudget", R"lua(
        sco.register_command{ name = "reloadbudget.ok", title = "OK", fn = function() return "fresh" end }
        sco.register_command{ name = "reloadbudget.spin", title = "Spin",
          fn = function() while true do pcall(function() while true do end end) end end }
        sco.register_command{ name = "reloadbudget.eat", title = "Eat", fn = function()
          local mb = string.rep(string.rep("x", 1000), 1000)
          local kept = {}; for i = 1, 100 do kept[i] = mb .. i end end }
    )lua");
    Loaded l = Load("reloadbudget");
    CHECK(l.p && l.p->state == State::Loaded);
    for (int round = 0; round < 2; ++round) {
        CHECK(Invoke(g_caller, "reloadbudget.ok").text == "fresh");
        const Reply spin = Invoke(g_caller, "reloadbudget.spin");
        CHECK(spin.r == SCO_CRASHED && spin.text.find("step budget") != std::string::npos);
        CHECK(!sco_lua_alive(l.p->self) && Invoke(g_caller, "reloadbudget.ok").r == SCO_UNAVAILABLE);
        CHECK(ReloadScript(l));
        CHECK(Invoke(g_caller, "reloadbudget.ok").text == "fresh");
        const Reply memory = Invoke(g_caller, "reloadbudget.eat");
        CHECK(memory.r == SCO_TOO_MANY && memory.text.find("not enough memory") != std::string::npos);
        CHECK(!sco_lua_alive(l.p->self) && Invoke(g_caller, "reloadbudget.ok").r == SCO_UNAVAILABLE);
        CHECK(ReloadScript(l));
    }
    Unload(l);
}

static void TestSettings() {
    // Without the service: sco.settings is there, get answers unavailable.
    Write("setnone", R"lua(
        local v, err, msg = sco.settings.get("speed")
        assert(v == nil and err == "unavailable" and msg == "sco.settings is not available on this host", tostring(msg))
        sco.log("info", "no settings: clean")
    )lua");
    Loaded none = Load("setnone");
    CHECK(none.p && none.p->state == State::Loaded && Logged("[setnone] no settings: clean"));
    Unload(none);

    CHECK(sco::settings::Start() == Result::Ok);
    Write("setting", R"lua(
        assert(sco.settings.set == nil and sco.settings.put == nil, "read-only")
        local speed = sco.settings.get("speed")
        assert(speed == 5 and math.type(speed) == "integer", tostring(speed))
        local fov = sco.settings.get("fov")
        assert(fov == 90.5 and math.type(fov) == "float", tostring(fov))
        assert(sco.settings.get("god_mode") == false)
        assert(sco.settings.get("nick") == "Pilot")
        assert(sco.settings.get("mode") == "normal")
        local v, err, msg = sco.settings.get("nope")
        assert(v == nil and err == "not_found" and msg == "this plugin declares no setting 'nope'", tostring(msg))
        v, err, msg = sco.settings.get("bad\0name")
        assert(v == nil and err == "bad_arg" and msg == "a setting name with a NUL byte", tostring(msg))
        assert(not pcall(sco.settings.get))        -- a name is required
        sco.register_command{ name = "setting.speed", title = "Speed",
          fn = function() return tostring(sco.settings.get("speed")) .. " " .. sco.settings.get("mode") end }
        sco.log("info", "settings: clean")
    )lua", "[settings]\nspeed = int default 5 min 1 max 10\nfov = float default 90.5\ngod_mode = bool\n"
           "nick = string default \"Pilot\"\nmode = enum(easy,normal,hard) default normal\n");
    Loaded l = Load("setting");
    CHECK(l.p && l.p->state == State::Loaded && Logged("[setting] settings: clean"));
    if (l.p && l.p->state != State::Loaded) std::printf("  setting: %s\n", l.p->reason.c_str());
    // The script reads what the host keeps now.
    CHECK(Invoke(g_caller, "setting.speed").text == "5 normal");
    CHECK(sco::settings::SetInt("setting", "speed", 8) == Result::Ok && sco::settings::SetString("setting", "mode", "hard") == Result::Ok);
    CHECK(Invoke(g_caller, "setting.speed").text == "8 hard");
    Unload(l);
    // A plugin that declares nothing finds nothing, with the service up.
    Write("setempty", R"lua(
        local v, err = sco.settings.get("speed")
        assert(v == nil and err == "not_found")
        sco.log("info", "no declarations: clean")
    )lua");
    Loaded e = Load("setempty");
    CHECK(e.p && e.p->state == State::Loaded && Logged("[setempty] no declarations: clean"));
    Unload(e);
    sco::settings::Stop();
}

// Synthetic 1.1 providers exercise both handshakes independently of SQLite limits.
static sco_storage_v1 g_schema;
static int g_schemaMode = 0, g_schemaCalls = 0, g_schemaErrors = 0;
static std::string g_schemaName, g_schemaType;
static sco_result SchemaError(sco_plugin*, char* out, uint32_t* size) {
    if (*size) out[0] = '\0';
    *size = 1; return SCO_OK;
}
static sco_result SchemaQuery(const char* name, uint32_t version, const void** out) {
    if (std::strcmp(name, SCO_STORAGE_NAME) == 0) { *out = &g_schema; return SCO_OK; }
    return g_api->query_service(name, version, out);
}
static sco_result SchemaTable(sco_plugin*, const char* after, char* out, uint32_t* size, uint32_t* kind) {
    ++g_schemaCalls;
    if (g_schemaMode == 5) {
        if (!after) { out[0] = '\0'; *size = 1; *kind = 1; return SCO_OK; }
        if (std::strcmp(after, "") == 0) {
            std::memcpy(out, "later", 6); *size = 6; *kind = 2; return SCO_OK;
        }
        if (std::strcmp(after, "later") != 0) ++g_schemaErrors;
        return SCO_NOT_FOUND;
    }
    if (after && g_schemaMode >= 6 && g_schemaMode <= 9) {
        if (g_schemaMode == 6) *size = 0;
        else if (g_schemaMode == 7) ++*size;
        else if (g_schemaMode == 8) { out[0] = 'x'; *size = 1; }
        else { out[0] = '\0'; out[1] = '\0'; *size = 2; }
        return SCO_OK;
    }
    if (after && std::strcmp(after, "first") != 0 && std::strcmp(after, g_schemaName.c_str()) != 0) ++g_schemaErrors;
    if (g_schemaMode == 1 && after) { *size = 16u * 1024u * 1024u + 2; return SCO_TOO_MANY; }
    if (g_schemaMode == 2 && after) return SCO_TOO_MANY;
    if (g_schemaMode == 3) { ++*size; return SCO_TOO_MANY; }
    if (after && std::strcmp(after, g_schemaName.c_str()) == 0) return SCO_NOT_FOUND;
    const std::string& value = after ? g_schemaName : std::string("first");
    const uint32_t needed = static_cast<uint32_t>(value.size() + 1);
    if (*size < needed) { *size = needed; return SCO_TOO_MANY; }
    std::memcpy(out, value.c_str(), needed); *size = needed; *kind = after ? 2 : 1;
    return SCO_OK;
}
static sco_result SchemaColumn(sco_plugin*, const char* table, uint32_t* index, char* name, uint32_t* ns,
                               char* type, uint32_t* ts, uint32_t* flags) {
    ++g_schemaCalls;
    if (g_schemaMode == 5) {
        if (std::strcmp(table, "") != 0) ++g_schemaErrors;
        if (*index) return SCO_NOT_FOUND;
        name[0] = '\0'; *ns = 1; type[0] = '\0'; *ts = 1;
        *flags = 0; ++*index; return SCO_OK;
    }
    if (*index == 1 && g_schemaMode >= 6 && g_schemaMode <= 10) {
        name[0] = 'x'; name[1] = '\0'; *ns = 2;
        type[0] = 't'; type[1] = '\0'; *ts = 2;
        if (g_schemaMode == 6) *ns = 0;
        else if (g_schemaMode == 8) { name[0] = 'x'; *ns = 1; }
        else if (g_schemaMode == 9) { type[0] = '\0'; type[1] = '\0'; *ts = 2; }
        if (g_schemaMode == 7) *ts = SCO_STORAGE_MAX_KEY + 2;
        if (g_schemaMode != 10) ++*index;
        return SCO_OK;
    }
    if (g_schemaMode == 4) {
        if (*ns < 16u * 1024u * 1024u + 1u) { *ns = 16u * 1024u * 1024u + 1u; return SCO_TOO_MANY; }
        *ts = 16u * 1024u * 1024u + 1u; return SCO_TOO_MANY;
    }
    if (*index > 1) return SCO_NOT_FOUND;
    if (*index == 1 && g_schemaMode == 1) { *ns = 16u * 1024u * 1024u + 2; *index = 99; return SCO_TOO_MANY; }
    if (*index == 1 && g_schemaMode == 2) { *index = 99; return SCO_TOO_MANY; }
    const std::string n = *index ? g_schemaName : std::string(256, 'n');
    const std::string t = *index ? g_schemaType : std::string(255, 't');
    uint32_t need = static_cast<uint32_t>(n.size() + 1);
    if (*ns < need) { *ns = need; *index = 99; return SCO_TOO_MANY; }
    std::memcpy(name, n.c_str(), need); *ns = need;
    need = static_cast<uint32_t>(t.size() + 1);
    if (*ts < need) { *ts = need; *index = 99; return SCO_TOO_MANY; }
    std::memcpy(type, t.c_str(), need); *ts = need;
    *flags = *index + 1; ++*index; return SCO_OK;
}
static void TestStoreSchema() {
    g_schema = *sco::storage::Table();
    g_schema.last_error = SchemaError; g_schema.next_table = SchemaTable; g_schema.next_column = SchemaColumn;
    g_schemaName.clear(); g_schemaType.clear();
    for (int i = 0; i < 160; ++i) { g_schemaName += "\xc3\xa9"; g_schemaType += "\xe6\xbc\xa2"; }
    sco_api api = *g_api; api.query_service = SchemaQuery;
    Write("schemalong", R"lua(
        local names, kinds = sco.store.tables()
        assert(#names == 2 and names[1] == "first" and names[2] == string.rep("é", 160))
        assert(kinds[1] == 1 and kinds[2] == 2)
        local cols, types, flags = sco.store.columns("first")
        assert(#cols == 2 and cols[1] == string.rep("n",256) and types[1] == string.rep("t",255))
        assert(cols[2] == string.rep("é",160) and types[2] == string.rep("漢",160))
        assert(flags[1] == 1 and flags[2] == 2)
    )lua");
    g_schemaMode = 0; g_schemaCalls = g_schemaErrors = 0;
    Loaded l = Load("schemalong", &api);
    CHECK(l.p && l.p->state == State::Loaded && g_schemaErrors == 0 && g_schemaCalls == 10);
    if (l.p && l.p->state != State::Loaded) std::printf("  schemalong: %s\n", l.p->reason.c_str());
    Unload(l);
    Write("schemaempty", R"lua(
        local names, kinds = sco.store.tables()
        assert(#names == 2 and names[1] == "" and names[2] == "later")
        assert(kinds[1] == 1 and kinds[2] == 2)
        local names2, types, flags = sco.store.columns("")
        assert(#names2 == 1 and names2[1] == "" and types[1] == "" and flags[1] == 0)
    )lua");
    g_schemaMode = 5; g_schemaCalls = g_schemaErrors = 0;
    Loaded empty = Load("schemaempty", &api);
    CHECK(empty.p && empty.p->state == State::Loaded && g_schemaErrors == 0 && g_schemaCalls == 5);
    if (empty.p && empty.p->state != State::Loaded) std::printf("  schemaempty: %s\n", empty.p->reason.c_str());
    Unload(empty);
    Write("schemamalformed", R"lua(
        local a, e = sco.store.tables(); assert(a == nil and e == "failed")
        a, e = sco.store.columns("first"); assert(a == nil and e == "failed")
    )lua");
    for (int mode : {6, 7, 8, 9}) {
        g_schemaMode = mode;
        Loaded malformed = Load("schemamalformed", &api);
        CHECK(malformed.p && malformed.p->state == State::Loaded);
        if (malformed.p && malformed.p->state != State::Loaded)
            std::printf("  schemamalformed mode %d: %s\n", mode, malformed.p->reason.c_str());
        Unload(malformed);
    }
    Write("schemabadindex", R"lua(
        local a, e = sco.store.columns("first"); assert(a == nil and e == "failed")
    )lua");
    g_schemaMode = 10;
    Loaded badindex = Load("schemabadindex", &api);
    CHECK(badindex.p && badindex.p->state == State::Loaded);
    if (badindex.p && badindex.p->state != State::Loaded) std::printf("  schemabadindex: %s\n", badindex.p->reason.c_str());
    Unload(badindex);
    Write("schemafail", R"lua(
        local a, e = sco.store.tables(); assert(a == nil and e == "too_many")
        a, e = sco.store.columns("first"); assert(a == nil and e == "too_many")
    )lua");
    for (int mode : {1, 2}) {
        g_schemaMode = mode; g_schemaCalls = 0;
        Loaded f = Load("schemafail", &api);
        CHECK(f.p && f.p->state == State::Loaded && g_schemaCalls == 5);
        if (f.p && f.p->state != State::Loaded) std::printf("  schemafail: %s\n", f.p->reason.c_str());
        Unload(f);
    }
    Write("schemaold", R"lua(
        assert(select(2, sco.store.tables()) == "unavailable")
        assert(select(2, sco.store.columns("first")) == "unavailable")
    )lua");
    g_schema.size = static_cast<uint32_t>(offsetof(sco_storage_v1, next_table));
    Loaded old = Load("schemaold", &api); CHECK(old.p && old.p->state == State::Loaded); Unload(old);
    g_schema.size = sizeof(g_schema); g_schema.next_table = nullptr; g_schema.next_column = nullptr;
    Loaded missing = Load("schemaold", &api); CHECK(missing.p && missing.p->state == State::Loaded); Unload(missing);
    g_schema.next_table = SchemaTable;
    g_schemaMode = 3; g_schemaCalls = 0;
    Write("schemaretry", R"lua(
        local a, e = sco.store.tables(); assert(a == nil and e == "too_many")
    )lua");
    Loaded retry = Load("schemaretry", &api);
    CHECK(retry.p && retry.p->state == State::Loaded && g_schemaCalls == 32);
    Unload(retry);
    g_schemaMode = 4; g_schemaCalls = 0;
    g_schema.next_column = SchemaColumn;
    Write("schemamemory", R"lua(
        local chunk, held = string.rep("x", 1024), {}
        for i = 1, 4 do held[i] = string.rep(chunk, 8192) end
        local ok = pcall(sco.store.columns, "first")
        assert(not ok, "schema buffers must count against the memory limit")
    )lua");
    Loaded memory = Load("schemamemory", &api);
    CHECK(memory.p && memory.p->state == State::Loaded && g_schemaCalls == 2);
    if (memory.p && memory.p->state != State::Loaded) std::printf("  schemamemory: %s\n", memory.p->reason.c_str());
    Unload(memory);
    g_schemaMode = 3; g_schemaCalls = 0;
    Write("schemabudget", R"lua(
        for i = 1, 1000 do sco.store.tables() end
    )lua");
    Loaded budget = Load("schemabudget", &api);
    CHECK(budget.p && budget.p->state == State::Refused && budget.p->reason.find("step budget") != std::string::npos);
    CHECK(g_schemaCalls < 11000);
    Unload(budget);
}

static void TestStore(const fs::path& sdk) {
    // A host without the service: sco.store is there, every call answers unavailable.
    static const char* const kNone = R"lua(
        assert(sco.store.available() == false)
        local v, err, msg = sco.store.get("a")
        assert(v == nil and err == "unavailable" and msg == "sco.storage is not available on this host", tostring(msg))
        assert(select(2, sco.store.put("a", "b")) == "unavailable")
        assert(select(2, sco.store.sql("SELECT 1")) == "unavailable")
        assert(select(2, sco.store.begin()) == "unavailable")
        sco.log("info", "no storage: clean")
    )lua";
    Write("stnone", kNone);
    Loaded none = Load("stnone");
    CHECK(none.p && none.p->state == State::Loaded && Logged("[stnone] no storage: clean"));
    if (none.p && none.p->state != State::Loaded) std::printf("  stnone: %s\n", none.p->reason.c_str());
    Unload(none);

    const fs::path data = g_root.parent_path() / "lua_storage";
    fs::remove_all(data);
    sco::storage::Options o;
    o.dataRoot = data;
    CHECK(sco::storage::Start(o) == Result::Ok);

    // An older host (sco_api 1.0, no query_service) while the service is published.
    sco_api old = *g_api;
    old.size = static_cast<uint32_t>(offsetof(sco_api, query_service));
    Write("stold", kNone);
    Loaded oldl = Load("stold", &old);
    CHECK(oldl.p && oldl.p->state == State::Loaded && Logged("[stold] no storage: clean"));
    Unload(oldl);

    TestStoreSchema();

    Write("storea", R"lua(
        local st = sco.store
        assert(st.available())
        -- key-value round trip
        assert(st.get("missing") == nil and select("#", st.get("missing")) == 1)
        assert(st.put("spots.home", "Lorville") and st.get("spots.home") == "Lorville")
        assert(st.put("spots.empty", "") and st.get("spots.empty") == "")
        assert(st.put("spots.bin", "a\0b") and st.get("spots.bin") == "a\0b")
        assert(st.put("shared", "a"))
        assert(st.delete("spots.empty") == true and st.delete("spots.empty") == false)
        local k = assert(st.keys("spots."))
        assert(#k == 2 and k[1] == "spots.bin" and k[2] == "spots.home", table.concat(k, ","))
        local page = assert(st.keys("spots.", "spots.bin", 1))
        assert(#page == 1 and page[1] == "spots.home")
        local v, err, msg = st.put(string.rep("k", 256), "x")
        assert(v == nil and err == "bad_arg", "key too long: " .. tostring(v) .. " " .. tostring(err) .. " " .. tostring(msg))
        assert(select(2, st.get("a\0b")) == "bad_arg", "NUL in a key")
        assert(select(2, st.keys(nil, nil, 0)) == "bad_arg", "limit")
        -- transactions
        assert(st.begin())
        assert(select(2, st.begin()) == "bad_arg", "begin twice")
        assert(st.put("tx.gone", "1") and st.rollback())
        assert(st.get("tx.gone") == nil)
        assert(st.begin() and st.put("tx.kept", "1") and st.commit())
        assert(st.get("tx.kept") == "1")
        assert(select(2, st.commit()) == "bad_arg", "commit with none open")
        -- SQL with parameters
        assert(st.exec("CREATE TABLE visits(place TEXT, at INTEGER, speed REAL, note TEXT)"))
        local tables, kinds = st.tables()
        assert(#tables == 1 and tables[1] == "visits" and kinds[1] == 1)
        local columns, types, flags = st.columns("visits")
        assert(#columns == 4 and columns[1] == "place" and types[1] == "TEXT" and flags[1] == 0)
        assert(columns[4] == "note" and types[4] == "TEXT")
        assert(#assert(st.columns("no_such_table")) == 0)
        assert(select(2, st.tables(0)) == "bad_arg")
        assert(st.exec("INSERT INTO visits VALUES (?, ?, ?, ?)", { "Lorville", 1, 2.5, n = 4 }) == 1)
        assert(st.exec("INSERT INTO visits VALUES (?, ?, ?, ?)", { "Area18", 2, 0.5, "rain" }) == 1)
        assert(st.exec("INSERT INTO visits VALUES (:p, :a, 0, NULL)", { "Orison", true }) == 1)
        local rows = assert(st.sql("SELECT place, at, speed, note FROM visits WHERE at >= ? ORDER BY at DESC, place", { 1 }))
        assert(#rows == 3 and rows[1].place == "Area18" and rows[1].note == "rain" and rows[1].at == 2)
        assert(rows[2].place == "Lorville" and math.type(rows[2].at) == "integer" and rows[2].at == 1)
        assert(rows[2].speed == 2.5 and rows[2].note == nil and rows[3].place == "Orison" and rows[3].at == 1)
        local one = assert(st.sql("SELECT value FROM sco_kv WHERE key = ?", { "spots.bin" }))
        assert(#one == 1 and one[1].value == "a\0b", "a BLOB from the key-value table")
        assert(#assert(st.sql("SELECT * FROM visits WHERE place = ?", { "nowhere" })) == 0)
        v, err, msg = st.sql("ATTACH DATABASE 'x.db' AS x")
        assert(v == nil and err == "bad_arg" and type(msg) == "string", "ATTACH refused")
        assert(select(2, st.sql("SELECT ?", {})) == "bad_arg", "parameter count")
        assert(select(2, st.sql("SELECT ?", { {} })) == "bad_arg", "a table as a parameter")
        -- bounded: more than 1000 rows as a table is too_many; a function sees them all
        assert(st.begin())
        assert(st.exec("CREATE TABLE n(i INTEGER)"))
        for i = 1, 1001 do assert(st.exec("INSERT INTO n VALUES (?)", { i })) end
        assert(st.commit())
        assert(select(2, st.sql("SELECT i FROM n")) == "too_many")
        local count = 0
        assert(st.sql("SELECT i FROM n", nil, function(r) count = count + r.i end) == true)
        assert(count == 1001 * 1002 // 2, "rows by function")
        count = 0
        assert(st.sql("SELECT i FROM n ORDER BY i", nil, function(r) count = r.i; return r.i < 3 end))
        assert(count == 3, "a function returning false stops")
        sco.register_command{ name = "storea.get", title = "Get", args = {{ name = "k", type = "string" }},
                              fn = function(key) return st.get(key) end }
        -- A script error mid-iteration: 70 times, more than the 64 cursors a plugin may hold.
        sco.register_command{ name = "storea.boom", title = "Boom", fn = function()
          for i = 1, 70 do
            local ok, e = pcall(st.sql, "SELECT i FROM n", nil, function(r) if r.i == 2 then error("mid-iteration") end end)
            if ok or not tostring(e):find("mid-iteration", 1, true) then return "pass " .. i .. ": " .. tostring(e) end
          end
          return #assert(st.sql("SELECT i FROM n LIMIT 5")) .. " rows after 70 errors"
        end }
    )lua");
    Loaded a = Load("storea");
    CHECK(a.p && a.p->state == State::Loaded);
    if (a.p && a.p->state != State::Loaded) std::printf("  storea: %s\n", a.p->reason.c_str());
    const Reply boom = Invoke(g_caller, "storea.boom");
    CHECK(boom.r == SCO_OK && boom.text == "5 rows after 70 errors");
    if (boom.text != "5 rows after 70 errors") std::printf("  storea.boom -> %d %s\n", boom.r, boom.text.c_str());
    CHECK(a.p && AllCursorsFree(a.p->self));

    // Isolation: a second script has its own keys and tables.
    Write("storeb", R"lua(
        local st = sco.store
        assert(st.get("shared") == nil and st.get("spots.home") == nil, "another plugin's keys")
        assert(st.put("shared", "b"))
        assert(#assert(st.keys()) == 1)
        local v, err = st.sql("SELECT * FROM visits")
        assert(v == nil and (err == "bad_arg" or err == "failed"), "another plugin's table: " .. tostring(err))
    )lua");
    Loaded b = Load("storeb");
    CHECK(b.p && b.p->state == State::Loaded);
    if (b.p && b.p->state != State::Loaded) std::printf("  storeb: %s\n", b.p->reason.c_str());
    CHECK(Invoke(g_caller, "storea.get", { Str("shared") }).text == "a");
    Unload(b);
    Unload(a);

    // Committed data outlives the script: a reload sees it.
    Write("storea", R"lua(
        sco.register_command{ name = "storea.get", title = "Get", args = {{ name = "k", type = "string" }},
                              fn = function(key) return sco.store.get(key) end }
    )lua");
    a = Load("storea");
    CHECK(Invoke(g_caller, "storea.get", { Str("spots.home") }).text == "Lorville");
    CHECK(Invoke(g_caller, "storea.get", { Str("tx.gone") }).text.empty());
    Unload(a);

    // The step budget running out inside a row callback: the script is disabled, its cursor closed.
    Write("stspin", R"lua(
        assert(sco.store.exec("CREATE TABLE IF NOT EXISTS t(i INTEGER)"))
        assert(sco.store.exec("INSERT INTO t VALUES (1), (2)"))
        sco.register_command{ name = "stspin.spin", title = "Spin", fn = function()
          sco.store.sql("SELECT i FROM t", nil, function() while true do end end) end }
    )lua");
    Loaded spin = Load("stspin");
    CHECK(spin.p && spin.p->state == State::Loaded);
    const Reply sr = Invoke(g_caller, "stspin.spin");
    CHECK(sr.r == SCO_CRASHED && sr.text.find("step budget") != std::string::npos);
    CHECK(spin.p && !sco_lua_alive(spin.p->self) && AllCursorsFree(spin.p->self));
    Unload(spin);

    // The SDK's storage example, sdk/examples/notebook: notes survive a reload.
    fs::copy(sdk / "examples/notebook", g_root / "notebook", fs::copy_options::recursive);
    Loaded nb = Load("notebook");
    CHECK(nb.p && nb.p->state == State::Loaded);
    CHECK(Invoke(g_caller, "notebook.add", { Str("buy fuel") }).text == "note 1 saved");
    CHECK(Invoke(g_caller, "notebook.add", { Str("visit Orison") }).text == "note 2 saved");
    Unload(nb);
    nb = Load("notebook");
    const Reply list = Invoke(g_caller, "notebook.list");
    CHECK(list.r == SCO_OK && list.text == "2 notes: buy fuel | visit Orison");
    if (list.text != "2 notes: buy fuel | visit Orison") std::printf("  notebook.list -> %d %s\n", list.r, list.text.c_str());
    CHECK(Invoke(g_caller, "notebook.clear").text == "removed 2 notes");
    CHECK(Invoke(g_caller, "notebook.list").text == "0 notes");
    Unload(nb);
    sco::storage::Stop();
}

#ifndef _WIN32
#include <unistd.h>
// A broken step budget turns the spin tests into endless loops: fail instead of hanging.
static void Deadline(int) {
    static const char msg[] = "FAIL test_lua: still running after 120 s (step budget not enforced?)\n";
    (void)!write(1, msg, sizeof(msg) - 1);
    _exit(1);
}
#endif

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: test_lua <sdk dir> <out dir>\n"); return 2; }
#ifndef _WIN32
    std::signal(SIGALRM, Deadline);
    alarm(120);
#endif
    sco::SetLogSink(Sink);
    sco::SetGameThread();
    g_api = sco::host::BuildApi({ "sco-lua test 1.0" });
    g_caller = sco::host::NewPlugin("caller");
    g_root = fs::path(argv[2]) / "lua-plugins";
    fs::remove_all(g_root);
    fs::create_directories(g_root);
    if (argc > 3 && strcmp(argv[3], "widgets") == 0) {
        CHECK(sco_lua_add_extension(&kWidgetTestExtension) == SCO_OK);
        TestWidgets();
        TestWidgetsTypedEvents();
        TestPanelBuilder();
        TestWidgetsServiceTables();
        TestWidgetsInvalid();
        TestWidgetsLifetime();
        TestWidgetsCallbackBoundary();
        TestWidgetsTails();
        TestWidgetsStaleCookies();
        TestWidgetsBounds();
        TestWidgetsExample(argv[1]);
        std::printf("sco-lua widgets tests: %d passed, %d failed\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    TestGreeterExample(argv[1]);
    TestTable();
    TestHotkeys();
    TestSandbox();
    TestLimits();
    TestTotalMemory();
    TestReloadLifecycle();
    TestReloadBudgets();
    TestExtensions();
#ifndef SCO_KERNEL_ONLY
#include "game_contracts/lua_run.inc"
#endif
    TestSettings();
    TestStore(argv[1]);
    std::printf("sco-lua tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
