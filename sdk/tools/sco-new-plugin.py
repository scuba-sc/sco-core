#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""sco-new-plugin: writes a ready-to-build plugin folder. Part of the sco SDK.

    python3 tools/sco-new-plugin.py --id my_radar --lang c|cpp|csharp|lua [--with-menu]
                                    [--services game.actors,sco.widgets] [--name TEXT]
                                    [--author TEXT] --out DIR

c and cpp: plugin.ini, the source (<id>.c in C11 or <id>.cpp in C++20), and a CMakeLists.txt
that builds it with the SDK's cmake/sco-plugin.cmake and adds a `check` target running
tools/sco-plugin-check on the built plugin twice: as is, and with --short-tables (a 1.0 host's
sco_api). Then:

    cmake -S DIR -B DIR/build -DSCO_SDK=<unpacked SDK, or sdk/ in a sco-core checkout>
    cmake --build DIR/build --target check

csharp: plugin.ini, <id>.csproj (a NativeAOT class library referencing the SDK's
csharp/Sco.Sdk), <Name>Plugin.cs and README.md. Then:

    dotnet publish DIR -c Release -r win-x64 -o DIR/out/<id> -p:ScoSdk=<SDK folder>
    <SDK folder>/out/bin/sco-plugin-check DIR/out/<id>   (and again with --short-tables)

lua: plugin.ini (kind = lua), main.lua and a CMakeLists.txt whose `check` target runs
sco-plugin-check on the folder; tools/lua-check.lua (Lua 5.4) runs the script against a stand-in.
The Lua sandbox has no generic service lookup, so --services is refused for lua (sco.widgets with
--with-menu is the menu's own and allowed).

The generated code checks every table's size before it touches a 1.1 member (the C# SDK and the
Lua runtime do it for their plugins), so it loads on a 1.0 host (plugin.ini says api = 1.0).
--with-menu adds a sco.widgets panel: built with scosdk::PanelBuilder in C++ and Sco.Sdk's
PanelBuilder in C#, as a sco_widget_node array in C and a node table in Lua. --services looks each
named service up on game.ready (NAME or NAME@MAJOR.MINOR, default 1.0); a missing one is logged,
never fatal.

Other tools can import this file and call render_plugin(spec), which returns
{relative path: file text} and writes nothing. Exit 0 = written, 2 = refused (reason printed).
"""
import argparse
import os
import re
import sys

ID_RE = re.compile(r"^[a-z0-9_]{1,31}$")
RESERVED_IDS = frozenset(("sco", "host", "menu", "game"))
SERVICE_RE = re.compile(r"^[a-z0-9_]+(\.[a-z0-9_]+)*$")
VERSION_RE = re.compile(r"^([0-9]{1,5})\.([0-9]{1,5})$")
TEXT_RE = re.compile(r"^[A-Za-z0-9 _.,()+-]{1,63}$")
MAX_SERVICES = 16
WIDGETS = "sco.widgets"
LANGS = ("c", "cpp", "csharp", "lua")
CS_NAMESPACE = "ScoPlugins"  # fixed: a namespace named after the id could hide a Sco.Sdk type


class SpecError(ValueError):
    """The spec asks for something the generator refuses (bad id, service, text or language)."""


def parse_service(text):
    name, _, version = text.strip().partition("@")
    if not SERVICE_RE.match(name) or len(name) > 63:
        raise SpecError(f"service '{text}': use 1-63 characters from a-z, 0-9, _ and ., no empty dotted parts")
    major, minor = 1, 0
    if version:
        m = VERSION_RE.match(version)
        if not m or int(m.group(1)) > 65535 or int(m.group(2)) > 65535:
            raise SpecError(f"service '{text}': the version is MAJOR.MINOR, each 0-65535")
        major, minor = int(m.group(1)), int(m.group(2))
    return name, major, minor


def normalize(spec):
    known = {"id", "lang", "with_menu", "services", "name", "author"}
    extra = sorted(set(spec) - known)
    if extra:
        raise SpecError(f"unknown spec key(s): {', '.join(extra)}")
    pid = spec.get("id")
    if not isinstance(pid, str) or not ID_RE.match(pid):
        raise SpecError(f"id {pid!r}: use 1-31 characters from a-z, 0-9 and _")
    if pid in RESERVED_IDS:
        raise SpecError(f"id '{pid}' is reserved")
    lang = spec.get("lang")
    if lang not in LANGS:
        raise SpecError(f"lang {lang!r}: use c, cpp, csharp or lua")
    with_menu = spec.get("with_menu", False)
    if not isinstance(with_menu, bool):
        raise SpecError("with_menu must be true or false")
    services = []
    for entry in spec.get("services") or ():
        name, major, minor = parse_service(entry) if isinstance(entry, str) else (None, 0, 0)
        if name is None:
            raise SpecError(f"service {entry!r} must be a string")
        if any(s[0] == name for s in services):
            raise SpecError(f"service '{name}' is listed twice")
        services.append((name, major, minor))
    if len(services) > MAX_SERVICES:
        raise SpecError(f"at most {MAX_SERVICES} services")
    name = spec.get("name") or " ".join(w for w in pid.split("_") if w).capitalize() or pid
    author = spec.get("author") or "you"
    for label, value in (("name", name), ("author", author)):
        if not isinstance(value, str) or not TEXT_RE.match(value) or value != value.strip():
            raise SpecError(f"{label} {value!r}: 1-63 characters from A-Z, a-z, 0-9, space and _.,()+-")
    s = {"id": pid, "lang": lang, "with_menu": with_menu, "services": services,
         "name": name, "author": author}
    # The same id, service and text rules hold for every language; this is what one can't do.
    if lang == "lua" and lookups(s):
        names = ", ".join(x[0] for x in lookups(s))
        raise SpecError(f"lang lua can't look up services ({names}): the Lua sandbox has no query_service, "
                        "only sco.widgets (with with_menu, --with-menu) and the sco.* tables in docs/lua.md. "
                        "Drop services (--services), or use c, cpp or csharp")
    return s


def class_name(pid):
    words = "".join(w[:1].upper() + w[1:] for w in pid.split("_"))
    return words if words[:1].isalpha() else "Plugin" + words


def version_hex(major, minor):
    return f"0x{(major << 16) | minor:08x}u"


def lookups(s):
    """Services to look up generically: sco.widgets is the menu's when --with-menu is on."""
    return [x for x in s["services"] if not (s["with_menu"] and x[0] == WIDGETS)]


def render_ini(s):
    what, why, kind, entry = {
        "csharp": ("native C# plugin (Sco.Sdk, NativeAOT)",
                   "Sco.Sdk checks sco_api's size before each 1.1 call", "native", f"{s['id']}.dll"),
        "lua": ("Lua plugin for the sco-lua sandbox",
                "the script needs nothing newer (sco.widgets answers unavailable without it)",
                "lua", "main.lua"),
    }.get(s["lang"], ("native plugin", "the code checks sco_api's size before each 1.1 call", "native",
                      f"{s['id']}.dll"))
    lines = [
        f"; {s['id']}: {what} generated by sco-new-plugin. Format: docs/plugin-ini.md in the SDK.",
        f"id = {s['id']}",
        f"name = {s['name']}",
        "version = 0.1.0",
        f"author = {s['author']}",
        f"; 1.0: {why}, so a 1.0 host loads it too.",
        "api = 1.0",
        f"kind = {kind}",
        f"entry = {entry}",
    ]
    names = [x[0] for x in s["services"]]
    if names:
        lines.append("; The services are optional here: the plugin logs a missing one and keeps working. To make")
        lines.append("; the host refuse to load it without them (and load it after their providers), uncomment:")
        lines.append(f"; requires = {', '.join(names)}")
    else:
        lines.append("; requires = teleport          ; optional: capabilities, services or plugin ids it needs")
    return "\n".join(lines) + "\n"


def cmake_head(pid, langs):
    return f"""\
cmake_minimum_required(VERSION 3.20)
project({pid} {langs})

# The unpacked sco SDK (the folder with cmake/, include/ and tools/), or sdk/ in a sco-core
# checkout: cmake -S . -B build -DSCO_SDK=<path>. The SCO_SDK environment variable works too.
set(SCO_SDK "$ENV{{SCO_SDK}}" CACHE PATH "Unpacked sco SDK")
if(NOT EXISTS "${{SCO_SDK}}/cmake/sco-plugin.cmake")
  message(FATAL_ERROR "Set SCO_SDK to the unpacked sco SDK: cmake -S . -B build -DSCO_SDK=<path>")
endif()
include("${{SCO_SDK}}/cmake/sco-plugin.cmake")
"""


def render_cmake(s):
    pid = s["id"]
    src = f"{pid}.c" if s["lang"] == "c" else f"{pid}.cpp"
    langs = "C" if s["lang"] == "c" else "C CXX"
    return cmake_head(pid, langs) + f"""
sco_add_plugin({pid} SOURCES {src} INI plugin.ini)

# `cmake --build build --target check`: the plugin is built into build/check/{pid}/ next to a copy
# of plugin.ini (sco-plugin-check wants the folder named after the id), then checked against the
# stand-in host twice: with this SDK's sco_api, and with --short-tables (a 1.0 host's sco_api).
# The stand-in host is not the game; a plugin that passes still needs a run in the game.
set(_sco_check_dir "${{CMAKE_CURRENT_BINARY_DIR}}/check/{pid}")
set_target_properties({pid} PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<1:${{_sco_check_dir}}>"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${{_sco_check_dir}}>")
configure_file(plugin.ini "${{_sco_check_dir}}/plugin.ini" COPYONLY)
""" + cmake_checker() + f"""
add_custom_target(check
  COMMAND sco-plugin-check "${{_sco_check_dir}}"
  COMMAND sco-plugin-check "${{_sco_check_dir}}" --short-tables
  DEPENDS {pid} sco-plugin-check
  VERBATIM)
"""


def render_cmake_lua(s):
    pid = s["id"]
    # C only for sco-plugin-check: the plugin itself compiles nothing.
    return cmake_head(pid, "C") + f"""
# Nothing to compile: `cmake --install build --prefix <out>` copies plugin.ini and main.lua into
# <out>/data/plugins/{pid}/, the folder to copy into sc-offline's data/plugins/.
sco_add_pack({pid} DIR "${{CMAKE_CURRENT_SOURCE_DIR}}")

# `cmake --build build --target check`: plugin.ini and main.lua are copied into build/check/{pid}/
# (sco-plugin-check wants the folder named after the id), then sco-plugin-check reads plugin.ini
# with the host's rules and finds the entry script. It does not run Lua: for that, with Lua 5.4,
#   lua5.4 <SDK>/tools/lua-check.lua <this folder>
# runs the script against a stand-in sco table. Neither is the game; a plugin that passes still
# needs a run in the game.
set(_sco_check_dir "${{CMAKE_CURRENT_BINARY_DIR}}/check/{pid}")
configure_file(plugin.ini "${{_sco_check_dir}}/plugin.ini" COPYONLY)
configure_file(main.lua "${{_sco_check_dir}}/main.lua" COPYONLY)
""" + cmake_checker() + """
add_custom_target(check
  COMMAND sco-plugin-check "${_sco_check_dir}"
  DEPENDS sco-plugin-check
  VERBATIM)
"""


def cmake_checker():
    return """
if(NOT TARGET sco-plugin-check)
  add_executable(sco-plugin-check EXCLUDE_FROM_ALL "${SCO_SDK}/tools/sco-plugin-check.c")
  target_include_directories(sco-plugin-check PRIVATE "${SCO_SDK_INCLUDE_DIR}")
  # gnu11: the checker uses POSIX lstat on macOS and Linux.
  set_target_properties(sco-plugin-check PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS ON MSVC_RUNTIME_LIBRARY "MultiThreaded")
  if(MSVC)
    target_compile_options(sco-plugin-check PRIVATE /W4 /WX /wd5105 /D_CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(sco-plugin-check PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(sco-plugin-check PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()
"""


def render_c(s):
    pid, name, author = s["id"], s["name"], s["author"]
    svcs = lookups(s)
    menu = s["with_menu"]
    out = [f"""\
/*
 * {pid}: native plugin generated by sco-new-plugin (C11).
 *
 * Build and check: see CMakeLists.txt. Reference: docs/api-v1.md in the SDK.
 * sco-core is GPL-3.0-only; see LICENSE and docs/plugin-rules.md in the SDK
 * for the project's plugin distribution policy. No linking exception is granted.
 *
 * sco_api 1.1 members (query_service, provide_service, ...) are only called after checking
 * api->size covers them, so the plugin also loads on a 1.0 host (sco-plugin-check --short-tables).
 * Every service table starts with uint32_t size: check it covers a member before calling it.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
"""]
    out.append('#include "sco_widgets.h"\n' if menu else '#include "sco_api.h"\n')
    out.append(f"""
static const sco_api* api;  /* valid from sco_plugin_load until sco_plugin_unload */
static sco_plugin*    me;   /* pass back to every call that takes `self` */

static const sco_plugin_info info = {{
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
    "{pid}",  /* = id in plugin.ini */
    "0.1.0",
    "{author}",
}};
""")
    if menu or svcs:
        out.append("""
/* True when the host's sco_api has the 1.1 member at offset (with size bytes). */
static int api_covers(size_t offset, size_t size) {
    return api && api->size >= offset + size;
}

static void logf_(sco_log_level level, const char* fmt, const char* a, unsigned b) {
    char line[160];
    snprintf(line, sizeof line, fmt, a, b);
    api->log(me, level, line);
}
""")
    if svcs:
        names = ", ".join(f'"{n}"' for n, _, _ in svcs)
        versions = ", ".join(version_hex(ma, mi) for _, ma, mi in svcs)
        out.append(f"""
/* Services looked up on game.ready. A table is borrowed: its provider may unload, so look it up
 * again when you need it. Include the service's header and cast to its table type to use one. */
#define SERVICE_COUNT {len(svcs)}u
static const char* const service_names[SERVICE_COUNT] = {{ {names} }};
static const uint32_t    service_versions[SERVICE_COUNT] = {{ {versions} }};
static const void*       services[SERVICE_COUNT];

static void lookup_services(void) {{
    uint32_t i;
    for (i = 0; i < SERVICE_COUNT; ++i) {{
        const void* table = NULL;
        sco_result  r = SCO_UNAVAILABLE;
        services[i] = NULL;
        if (api_covers(offsetof(sco_api, query_service), sizeof(api->query_service)) && api->query_service)
            r = api->query_service(service_names[i], service_versions[i], &table);
        if (r == SCO_OK && table && *(const uint32_t*)table >= sizeof(uint32_t)) {{
            services[i] = table;
            logf_(SCO_LOG_INFO, "service %s found (table size %u)", service_names[i], *(const uint32_t*)table);
        }} else {{
            logf_(SCO_LOG_WARN, "service %s not available (result %u): carrying on without it", service_names[i],
                  (unsigned)r);
        }}
    }}
}}
""")
    out.append(f"""
static void on_ready(const char* event, const void* data, void* ctx) {{
    (void)event; (void)data; (void)ctx;
""")
    if svcs:
        out.append("    lookup_services();\n")
    out.append(f"""    api->status(me, "{name} ready");
}}

/* {pid}.ping: a command with no arguments. Runs on the game thread. */
static sco_result ping(const sco_arg* args, uint32_t nargs, void* ctx, char* reply, uint32_t reply_size) {{
    (void)args; (void)ctx;
    if (nargs != 0) return SCO_BAD_ARG;
""")
    if svcs:
        out.append("""    {
        uint32_t i, found = 0;
        for (i = 0; i < SERVICE_COUNT; ++i) found += services[i] != NULL;
        if (reply && reply_size) snprintf(reply, reply_size, "pong (%u of %u services)", (unsigned)found, SERVICE_COUNT);
    }
""")
    else:
        out.append('    if (reply && reply_size) snprintf(reply, reply_size, "pong");\n')
    out.append("    return SCO_OK;\n}\n")
    if menu:
        out.append(f"""
/* The panel: sco.widgets 1.0, found with query_service (sco_api 1.1). The host copies the nodes
 * and strings; events update this model, and set_panel would replace the panel whole. */
#define PANEL_ID "{pid}.main"
static const sco_widgets_v1* widgets;
static int                   enabled;
static double                amount = 0.5;

static void on_widget(const char* panel, const sco_widget_event* event, void* ctx) {{
    (void)panel; (void)ctx;
    if (!event || event->size < sizeof(*event)) return;
    switch (event->kind) {{
    case SCO_WIDGET_TOGGLE: enabled = event->value != 0; break;
    case SCO_WIDGET_SLIDER: amount = event->value; break;
    case SCO_WIDGET_BUTTON: logf_(SCO_LOG_INFO, "%s button: node %u", "{pid}", (unsigned)event->node_id); break;
    default: break;
    }}
}}

static void show_panel(void) {{
    const void*     table = NULL;
    sco_widget_node nodes[4];
    uint32_t        i;
    sco_result      r;
    widgets = NULL;
    /* An optional service: without it the plugin still works, just without a panel. */
    if (!api_covers(offsetof(sco_api, query_service), sizeof(api->query_service)) || !api->query_service ||
        api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) != SCO_OK || !table) {{
        api->log(me, SCO_LOG_INFO, "sco.widgets not available: no panel");
        return;
    }}
    widgets = (const sco_widgets_v1*)table;
    if (widgets->size < offsetof(sco_widgets_v1, remove_panel) + sizeof(widgets->remove_panel) ||
        !widgets->set_panel || !widgets->remove_panel) {{
        widgets = NULL;
        return;
    }}
    /* Preorder: ids are nonzero and unique, parent 0 is the root, otherwise a preceding ROW/COLUMN. */
    memset(nodes, 0, sizeof nodes);
    for (i = 0; i < 4; ++i) {{ nodes[i].size = sizeof(nodes[i]); nodes[i].id = i + 1; }}
    nodes[0].kind = SCO_WIDGET_COLUMN;
    nodes[1].kind = SCO_WIDGET_TOGGLE; nodes[1].parent = 1; nodes[1].label = "Enabled";
    nodes[1].value = enabled ? 1.0 : 0.0;
    nodes[2].kind = SCO_WIDGET_SLIDER; nodes[2].parent = 1; nodes[2].label = "Amount";
    nodes[2].value = amount; nodes[2].minimum = 0.0; nodes[2].maximum = 1.0;
    nodes[3].kind = SCO_WIDGET_BUTTON; nodes[3].parent = 1; nodes[3].label = "Ping";
    r = widgets->set_panel(me, PANEL_ID, "{name}", nodes, 4, sizeof(nodes[0]), on_widget, NULL);
    if (r != SCO_OK) {{
        logf_(SCO_LOG_WARN, "%s: set_panel refused (result %u)", PANEL_ID, (unsigned)r);
        widgets = NULL;
    }}
}}
""")
    out.append(f"""
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {{ return &info; }}

SCO_EXPORT sco_result sco_plugin_load(const sco_api* a, sco_plugin* self) {{
    sco_command cmd;
    sco_result  r;

    api = a;
    me = self;

    memset(&cmd, 0, sizeof cmd);
    cmd.size         = sizeof cmd;
    cmd.name         = "{pid}.ping";
    cmd.title        = "Ping";
    cmd.help         = "Answers pong";
    cmd.arg_def_size = sizeof(sco_arg_def);
    cmd.fn           = ping;
    r = api->register_command(me, &cmd);
    if (r != SCO_OK) return r;

    r = api->subscribe(me, "game.ready", on_ready, NULL);
    if (r != SCO_OK) return r;
""")
    if menu:
        out.append("    show_panel();\n")
    out.append("""
    api->log(me, SCO_LOG_INFO, "loaded");
    return SCO_OK;  /* anything else: the host unloads the plugin */
}

""")
    if menu:
        out.append("""SCO_EXPORT void sco_plugin_unload(void) {
    if (widgets) (void)widgets->remove_panel(me, PANEL_ID);
    widgets = NULL;
}
""")
    else:
        out.append("SCO_EXPORT void sco_plugin_unload(void) {}\n")
    return "".join(out)


def render_cpp(s):
    pid, name, author = s["id"], s["name"], s["author"]
    cls = class_name(pid)
    svcs = lookups(s)
    menu = s["with_menu"]
    out = [f"""\
// {pid}: native plugin generated by sco-new-plugin (C++20, include/scosdk/).
//
// Build and check: see CMakeLists.txt. Reference: docs/sdk-cpp.md in the SDK.
// sco-core is GPL-3.0-only; see LICENSE and docs/plugin-rules.md in the SDK
// for the project's plugin distribution policy. No linking exception is granted.
//
// sco_api 1.1 members are reached only through scosdk calls that check the table's size first
// (ServiceRef::Query, Widgets::Open), so the plugin also loads on a 1.0 host
// (sco-plugin-check --short-tables). Service tables start with uint32_t size: test a member with
// sco::sdk::HasMember before calling it.
#include "scosdk/scosdk.hpp"
"""]
    if menu:
        out.append('#include "scosdk/menu_builder.hpp"\n')
    out.append("""
#include <cstdint>
""")
    if svcs:
        out.append("#include <array>\n")
    if menu:
        out.append("#include <utility>\n")
    out.append("""
namespace {
""")
    if svcs:
        rows = "\n".join(f'    ServiceName{{"{n}", sco::sdk::ServiceVersion({ma}, {mi})}},' for n, ma, mi in svcs)
        out.append(f"""
// Any service table: they all start with uint32_t size. Include the service's header and use its
// own table type in a ServiceRef to call it.
struct AnyTable {{
    uint32_t size;
}};

struct ServiceName {{
    const char* name;
    uint32_t    version;
}};

constexpr std::array<ServiceName, {len(svcs)}> kServices{{{{
{rows}
}}}};
""")
    capture = "this" if svcs else ""
    out.append(f"""
class {cls} : public sco::sdk::Plugin {{
public:
    sco_result OnLoad() override {{
        sco_result r = sco::sdk::CommandBuilder(*this, "{pid}.ping")
                           .Title("Ping")
                           .Help("Answers pong")
                           .Handle([{capture}](const sco::sdk::Args&, sco::sdk::Reply& reply) {{
""")
    if svcs:
        out.append("""                               reply.Printf("pong (%u of %u services)", Found(),
                                            static_cast<unsigned>(kServices.size()));
""")
    else:
        out.append('                               reply.Set("pong");\n')
    out.append(f"""                               return SCO_OK;
                           }})
                           .Register();
        if (r != SCO_OK) return r;

        ready_ = Subscribe("game.ready", [this](const void*) {{ OnReady(); }});
        if (!ready_) return ready_.Result();
""")
    if menu:
        out.append("        ShowPanel();\n")
    out.append("""        Info("loaded");
        return SCO_OK;
    }
""")
    if menu:
        out.append(f"""
    void OnUnload() override {{
        if (widgets_) (void)widgets_.RemovePanel("{pid}.main");
    }}
""")
    out.append("""
private:
    void OnReady() {
""")
    if svcs:
        out.append("        LookupServices();\n")
    out.append(f"""        Status("{name} ready");
    }}
""")
    if svcs:
        out.append("""
    // A table is borrowed: its provider may unload, so look it up again when you need it.
    void LookupServices() {
        for (std::size_t i = 0; i < kServices.size(); ++i) {
            sco::sdk::ServiceRef<AnyTable>& ref = services_[i];
            const sco_result r = ref.Query(*this, kServices[i].name, kServices[i].version);
            if (r == SCO_OK && ref && ref->size >= sizeof(uint32_t))
                Info("service %s found (table size %u)", kServices[i].name, static_cast<unsigned>(ref->size));
            else {
                ref.Reset();
                Warn("service %s not available (result %d): carrying on without it", kServices[i].name,
                     static_cast<int>(r));
            }
        }
    }

    unsigned Found() const {
        unsigned n = 0;
        for (const auto& ref : services_) n += ref ? 1u : 0u;
        return n;
    }
""")
    if menu:
        out.append(f"""
    // The panel: sco.widgets 1.0. Widgets::Open checks sco_api's size before query_service.
    void ShowPanel() {{
        if (widgets_.Open(*this) != SCO_OK) {{
            Info("sco.widgets not available: no panel");
            return;
        }}
        scosdk::PanelBuilder panel;
        panel.Column([&] {{
            panel.Toggle("Enabled", &enabled_);
            panel.Slider("Amount", &amount_, 0.0, 1.0);
            panel.Button("Ping", [this] {{ Info("ping from the panel"); }});
        }});
        scosdk::PanelTree next = panel.Build();
        if (!next) {{
            Warn("panel: %s", next.Error().c_str());
            return;
        }}
        // Swap only after the host accepted it: a refused set_panel keeps the old callback.
        if (next.Submit(widgets_, "{pid}.main", "{name}") == SCO_OK) tree_ = std::move(next);
    }}
""")
    out.append("\n    sco::sdk::Subscription ready_;\n")
    if svcs:
        out.append(f"    std::array<sco::sdk::ServiceRef<AnyTable>, {len(svcs)}> services_{{}};\n")
    if menu:
        out.append("""    sco::sdk::Widgets      widgets_;
    scosdk::PanelTree      tree_;  // lives as long as the registered panel
    bool                   enabled_ = false;
    double                 amount_ = 0.5;
""")
    out.append(f"""}};

}}  // namespace

// id must equal plugin.ini's id; it is also the command prefix.
SCO_PLUGIN({cls}, "{pid}", "0.1.0", "{author}");
""")
    return "".join(out)


def cs_class(pid):
    return class_name(pid) + "Plugin"


def render_csproj(s):
    pid = s["id"]
    return f"""\
<Project Sdk="Microsoft.NET.Sdk">

  <!-- {pid}: a native sco plugin in C#, generated by sco-new-plugin. Published with NativeAOT into
       one self-contained {pid}.dll with the three C exports; no .NET runtime is needed on the
       player's machine. ScoSdk is the unpacked sco SDK (the folder with csharp/ and tools/), or
       sdk/ in a sco-core checkout; the SCO_SDK environment variable works too:

         dotnet publish -c Release -r win-x64 -o out/{pid} -p:ScoSdk=<SDK folder>

       The publish copies plugin.ini next to {pid}.dll. See README.md and docs/sdk-csharp.md. -->
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <OutputType>Library</OutputType>
    <AssemblyName>{pid}</AssemblyName>
    <RootNamespace>{CS_NAMESPACE}</RootNamespace>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Nullable>enable</Nullable>
    <ImplicitUsings>disable</ImplicitUsings>
    <LangVersion>12</LangVersion>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>

    <!-- NativeAOT: a native shared library. Plugins are 64-bit Windows DLLs; sco-plugin-check
         also loads a linux-x64 build. -->
    <PublishAot>true</PublishAot>
    <NativeLib>Shared</NativeLib>
    <SelfContained>true</SelfContained>
    <!-- No ICU next to the game: culture data isn't shipped, every culture is invariant. -->
    <InvariantGlobalization>true</InvariantGlobalization>
    <UseSystemResourceKeys>true</UseSystemResourceKeys>

    <ScoSdk Condition="'$(ScoSdk)' == ''">$(SCO_SDK)</ScoSdk>
  </PropertyGroup>

  <!-- Faster NativeAOT compilation for development and CI: -p:SCO_FAST_AOT=1. -->
  <PropertyGroup Condition="'$(SCO_FAST_AOT)' == '1'">
    <IlcOptimizationPreference>Speed</IlcOptimizationPreference>
    <IlcGenerateStackTraceData>false</IlcGenerateStackTraceData>
    <DebugType>none</DebugType>
    <DebugSymbols>false</DebugSymbols>
  </PropertyGroup>

  <ItemGroup>
    <ProjectReference Include="$(ScoSdk)/csharp/Sco.Sdk/Sco.Sdk.csproj" />
    <None Update="plugin.ini" CopyToPublishDirectory="PreserveNewest" />
  </ItemGroup>

  <Target Name="ScoSdkRequired" BeforeTargets="_GenerateRestoreProjectSpec;CollectPackageReferences;ResolveProjectReferences"
          Condition="!Exists('$(ScoSdk)/csharp/Sco.Sdk/Sco.Sdk.csproj')">
    <Error Text="Set ScoSdk to the unpacked sco SDK (the folder with csharp/Sco.Sdk; the core SDK archive has it, the core-source kit does not): dotnet publish -p:ScoSdk=&lt;path&gt;, or the SCO_SDK environment variable." />
  </Target>

</Project>
"""


def render_cs(s):
    pid, name, author = s["id"], s["name"], s["author"]
    cls = cs_class(pid)
    svcs = lookups(s)
    menu = s["with_menu"]
    out = [f"""\
// {pid}: native plugin generated by sco-new-plugin (C#, Sco.Sdk, published with NativeAOT).
//
// Build and check: see README.md. Reference: docs/sdk-csharp.md in the SDK.
// sco-core is GPL-3.0-only; see LICENSE and docs/plugin-rules.md in the SDK
// for the project's plugin distribution policy. No linking exception is granted.
//
// sco_api 1.1 members are reached only through Sco.Sdk calls that check the table's size first
// (Query, Widgets.Open), so the plugin also loads on a 1.0 host (sco-plugin-check --short-tables).
// Service tables start with uint size: test a member with ServiceRef.Covers before calling it.
using System.Runtime.InteropServices;
using Sco.Sdk;
using Sco.Sdk.Interop;

namespace {CS_NAMESPACE}
{{
"""]
    if svcs:
        out.append("""\
    // Any service table: they all start with uint size. Declare the service's own table struct
    // (see docs/sdk-csharp.md, Services) and query with it to call its functions.
    [StructLayout(LayoutKind.Sequential)]
    public struct AnyTable
    {
        public uint size;
    }

""")
    out.append(f"""\
    public sealed class {cls} : Plugin
    {{
""")
    if svcs:
        rows = "\n".join(f'            ("{n}", ServiceVersion({ma}, {mi})),' for n, ma, mi in svcs)
        out.append(f"""\
        // Services looked up on game.ready. A table is borrowed: its provider may unload, so look it
        // up again when you need it.
        private static readonly (string Name, uint Version)[] Wanted =
        {{
{rows}
        }};

        private readonly bool[] _found = new bool[Wanted.Length];
""")
    if menu:
        out.append(f"""\
        private const string PanelId = "{pid}.main";
        private readonly Widgets _widgets = new();
        private bool _enabled;
        private double _amount = 0.5;
""")
    out.append(f"""\
        private Subscription? _ready;

        public override ScoResult OnLoad()
        {{
            // {pid}.ping: a command with no arguments. Runs on the game thread.
            ScoResult r = new CommandBuilder(this, "{pid}.ping")
                .Title("Ping")
                .Help("Answers pong")
                .Handle((args, reply) =>
                {{
""")
    if svcs:
        out.append("                    reply.Set($\"pong ({Found()} of {Wanted.Length} services)\");\n")
    else:
        out.append('                    reply.Set("pong");\n')
    out.append("""\
                    return ScoResult.Ok;
                })
                .Register();
            if (r != ScoResult.Ok) return r;

            _ready = Subscribe("game.ready", OnReady);
            if (_ready.Result != ScoResult.Ok) return _ready.Result;
""")
    if menu:
        out.append("            ShowPanel();\n")
    out.append("""\
            Info("loaded");
            return ScoResult.Ok;  // anything else: the host unloads the plugin
        }
""")
    if menu:
        out.append("""
        public override void OnUnload()
        {
            if (_widgets.IsOpen) _widgets.RemovePanel(PanelId);
        }
""")
    out.append("""
        private void OnReady()
        {
""")
    if svcs:
        out.append("            LookupServices();\n")
    out.append(f"""\
            Status("{name} ready");
        }}
""")
    if svcs:
        out.append("""
        private void LookupServices()
        {
            for (int i = 0; i < Wanted.Length; ++i)
            {
                ScoResult r = Query(Wanted[i].Name, Wanted[i].Version, out ServiceRef<AnyTable> table);
                _found[i] = r == ScoResult.Ok;
                if (_found[i]) Info($"service {Wanted[i].Name} found (table size {table.Size})");
                else Warn($"service {Wanted[i].Name} not available (result {r}): carrying on without it");
            }
        }

        private int Found()
        {
            int n = 0;
            foreach (bool found in _found) n += found ? 1 : 0;
            return n;
        }
""")
    if menu:
        out.append(f"""
        // The panel: sco.widgets 1.0. Widgets.Open checks sco_api's size before query_service; an
        // optional service: without it the plugin still works, just without a panel.
        private void ShowPanel()
        {{
            if (_widgets.Open(this) != ScoResult.Ok)
            {{
                Info("sco.widgets not available: no panel");
                return;
            }}
            // Preorder, explicit nonzero ids, stable across replacements.
            var panel = new PanelBuilder()
                .Column(1, column => column
                    .Toggle(2, "Enabled", _enabled, value => _enabled = value)
                    .Slider(3, "Amount", _amount, 0.0, 1.0, value => _amount = value)
                    .Button(4, "Ping", () => Info("ping from the panel")));
            ScoResult r = _widgets.SetPanel(PanelId, "{name}", panel.Build(), panel.Dispatch);
            if (r != ScoResult.Ok) Warn($"{{PanelId}}: set_panel refused (result {{r}})");
        }}
""")
    out.append(f"""\
    }}

    // The three exports. NativeAOT exports [UnmanagedCallersOnly(EntryPoint = ...)] methods of the
    // published assembly only, so they live here and forward to the SDK. "{pid}" must equal
    // plugin.ini's id; it is also the command prefix.
    public static unsafe class Exports
    {{
        [UnmanagedCallersOnly(EntryPoint = "sco_plugin_query")]
        public static ScoPluginInfo* Query() => PluginExports.Query("{pid}", "0.1.0", "{author}");

        [UnmanagedCallersOnly(EntryPoint = "sco_plugin_load")]
        public static ScoResult Load(ScoApi* api, void* self) => PluginExports.Load<{cls}>(api, self, "{pid}");

        [UnmanagedCallersOnly(EntryPoint = "sco_plugin_unload")]
        public static void Unload() => PluginExports.Unload();
    }}
}}
""")
    return "".join(out)


def render_cs_readme(s):
    pid, name = s["id"], s["name"]
    cls = cs_class(pid)
    extra = []
    if s["with_menu"]:
        extra.append(f"- a `sco.widgets` panel, `{pid}.main`, built with `PanelBuilder` when the host has the service,")
    if lookups(s):
        names = ", ".join(f"`{n}`" for n, _, _ in lookups(s))
        extra.append(f"- on `game.ready`, a lookup of {names}; a missing service is logged, never fatal,")
    lines = "\n".join(extra)
    return f"""\
# {name} (`{pid}`)

A native sco plugin in C#, generated by `sco-new-plugin`: [`{cls}.cs`]({cls}.cs) on the SDK's C# layer
(`csharp/Sco.Sdk`), published with NativeAOT into one native `{pid}.dll` with the three C exports.
No .NET runtime is needed on the player's machine.

- one command, `{pid}.ping`, which answers `pong`,
{lines + chr(10) if lines else ""}- `plugin.ini` with `api = 1.0`: Sco.Sdk checks sco_api's size before each 1.1 call.

## Build and check

You need the .NET 8 SDK (a newer SDK builds `net8.0` too) and, on Windows, Visual Studio 2019 or
newer with "Desktop development with C++" (NativeAOT links with MSVC). `ScoSdk` is the unpacked
core sco SDK (the folder with `csharp/` and `tools/`), or `sdk/` in a sco-core checkout; the
`SCO_SDK` environment variable works too. The SDK README's "Build the examples" step builds the
checker into `<SDK folder>\\out\\bin\\sco-plugin-check`.

```powershell
dotnet publish -c Release -r win-x64 -o out\\{pid} -p:ScoSdk=<SDK folder>
<SDK folder>\\out\\bin\\sco-plugin-check out\\{pid}
<SDK folder>\\out\\bin\\sco-plugin-check out\\{pid} --short-tables
```

The publish copies `plugin.ini` next to `{pid}.dll`. `--short-tables` hands the plugin a 1.0
host's `sco_api`. `sco-plugin-check` is a stand-in host, not the game: a plugin that passes still
needs a run in the game. Install by copying `out\\{pid}` (`{pid}.dll` and `plugin.ini`; leave the
`.pdb` files) into sc-offline's `data\\plugins\\`.

On Linux, `dotnet publish -r linux-x64` builds `{pid}.so` (needs `clang` and `zlib1g-dev`); rename
it to `{pid}.dll` for `sco-plugin-check`. The game needs the Windows build.

The C# SDK reference (`docs/sdk-csharp.md`) covers lifetimes, threads, the exception boundary and
the NativeAOT rules.
"""


def render_lua(s):
    pid, name = s["id"], s["name"]
    out = [f"""\
-- {pid}: Lua plugin generated by sco-new-plugin. sc-offline's sco-lua runtime runs this script
-- once at load, in a sandbox (no io, os, require or FFI); its callbacks run on the game thread.
--
-- Check: see CMakeLists.txt, and with Lua 5.4: lua5.4 <SDK>/tools/lua-check.lua <this folder>.
-- Reference: docs/lua.md in the SDK.
-- sco-core is GPL-3.0-only; see LICENSE and docs/plugin-rules.md in the SDK
-- for the project's plugin distribution policy.

-- {pid}.ping: a command with no arguments.
assert(sco.register_command{{
  name = "{pid}.ping",
  title = "Ping",
  help = "Answers pong",
  fn = function() return "pong" end,
}})

assert(sco.subscribe("game.ready", function()
  sco.status("{name} ready")
end))
"""]
    if s["with_menu"]:
        out.append(f"""
-- The panel: sco.widgets 1.0. available() is false without the host service; a runtime without
-- sco.widgets (and tools/lua-check.lua) has no table at all. Either way: no panel, commands work.
-- The host copies the nodes; an ordinary unload removes the panel before closing the script.
local enabled, amount = false, 0.5
if sco.widgets and sco.widgets.available() then
  -- Flat preorder nodes: ids are nonzero and unique, parent is 0 or a preceding row/column.
  local ok, err = sco.widgets.set_panel("{pid}.main", "{name}", {{
    {{kind = "column", id = 1}},
    {{kind = "toggle", id = 2, parent = 1, label = "Enabled", value = enabled}},
    {{kind = "slider", id = 3, parent = 1, label = "Amount", value = amount, minimum = 0, maximum = 1}},
    {{kind = "button", id = 4, parent = 1, label = "Ping"}},
  }}, function(panel, event)
    if event.kind == "toggle" then enabled = event.value
    elseif event.kind == "slider" then amount = event.value
    elseif event.kind == "button" then sco.log("info", panel .. ": ping from the panel") end
  end)
  if not ok then sco.log("warn", "{pid}.main: set_panel refused (" .. tostring(err) .. ")") end
else
  sco.log("info", "sco.widgets not available: no panel")
end
""")
    out.append("""
sco.log("info", "loaded")
""")
    return "".join(out)


def render_plugin(spec):
    """spec: {"id", "lang": "c"|"cpp"|"csharp"|"lua", "with_menu"?, "services"? [str], "name"?,
    "author"?}. Returns {relative path: text} for a ready-to-build plugin folder. Pure: writes
    nothing. Raises SpecError for an id, language, service, name or author the generator refuses,
    and for services with lang lua (the sandbox has no service lookup)."""
    s = normalize(spec)
    pid, lang = s["id"], s["lang"]
    if lang == "csharp":
        return {
            "plugin.ini": render_ini(s),
            f"{pid}.csproj": render_csproj(s),
            f"{cs_class(pid)}.cs": render_cs(s),
            "README.md": render_cs_readme(s),
        }
    if lang == "lua":
        return {
            "plugin.ini": render_ini(s),
            "CMakeLists.txt": render_cmake_lua(s),
            "main.lua": render_lua(s),
        }
    src = f"{pid}.c" if lang == "c" else f"{pid}.cpp"
    return {
        "plugin.ini": render_ini(s),
        "CMakeLists.txt": render_cmake(s),
        src: render_c(s) if lang == "c" else render_cpp(s),
    }


def check_relative(path):
    """A rendered path must stay inside the output folder: relative, no '..', no drive or '\\'."""
    parts = path.split("/")
    if (not path or path.startswith("/") or "\\" in path or ":" in path
            or any(p in ("", ".", "..") for p in parts)):
        raise SpecError(f"unsafe output path '{path}'")


def write_plugin(files, out):
    """Writes files (render_plugin's result) into out, which must be missing or an empty folder."""
    for rel in files:
        check_relative(rel)
    if os.path.islink(out):
        raise SpecError(f"{out}: is a symbolic link; give a real folder")
    if os.path.exists(out):
        if not os.path.isdir(out):
            raise SpecError(f"{out}: exists and is not a folder")
        if os.listdir(out):
            raise SpecError(f"{out}: exists and is not empty")
    else:
        os.makedirs(out)
    for rel, text in sorted(files.items()):
        path = os.path.join(out, *rel.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "x", encoding="utf-8", newline="\n") as f:  # "x": never overwrite
            f.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="sco-new-plugin", description="Writes a ready-to-build sco plugin folder (C, C++, C# or Lua).")
    ap.add_argument("--id", required=True, help="plugin id: 1-31 characters from a-z, 0-9 and _")
    ap.add_argument("--lang", required=True, choices=LANGS)
    ap.add_argument("--with-menu", action="store_true", help="add a sco.widgets panel")
    ap.add_argument("--services", default="", help="comma list of services to look up: NAME or NAME@MAJOR.MINOR")
    ap.add_argument("--name", help="display name (default: from the id)")
    ap.add_argument("--author", help="author (default: you)")
    ap.add_argument("--out", required=True, help="folder to create; must be missing or empty")
    args = ap.parse_args(argv)
    spec = {"id": args.id, "lang": args.lang, "with_menu": args.with_menu,
            "services": [x for x in args.services.split(",") if x.strip()]}
    if args.name is not None:
        spec["name"] = args.name
    if args.author is not None:
        spec["author"] = args.author
    try:
        files = render_plugin(spec)
        write_plugin(files, args.out)
    except (SpecError, OSError) as e:
        print(f"sco-new-plugin: {e}", file=sys.stderr)
        return 2
    sdk = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    print(f"wrote {', '.join(sorted(files))} to {args.out}")
    print("next:")
    if args.lang == "csharp":
        out = os.path.join(args.out, "out", args.id)
        if os.path.isfile(os.path.join(sdk, "csharp", "Sco.Sdk", "Sco.Sdk.csproj")):
            kit = sdk
        else:  # the core-source kit ships this tool but not the C# layer
            kit = "<core SDK folder>"
            print("  (this kit has no csharp/Sco.Sdk: point ScoSdk at an unpacked core SDK archive, which has it)")
        print(f'  dotnet publish "{args.out}" -c Release -r win-x64 -o "{out}" -p:ScoSdk="{kit}"')
        checker = os.path.join(kit, "out", "bin", "sco-plugin-check")
        print(f'  "{checker}" "{out}"   (and again with --short-tables; built by "Build the examples" in the SDK README)')
        return 0
    print(f'  cmake -S "{args.out}" -B "{os.path.join(args.out, "build")}" -DSCO_SDK="{sdk}"')
    print(f'  cmake --build "{os.path.join(args.out, "build")}" --target check')
    if args.lang == "lua":
        print(f'  lua5.4 "{os.path.join(sdk, "tools", "lua-check.lua")}" "{args.out}"   (needs Lua 5.4)')
    return 0


if __name__ == "__main__":
    sys.exit(main())
