/* Test plugin for sco-plugin-check's stand-in host (tests/plugin_check.cmake). Built once per case
 * with -DPROBE_MODE=<n> and -DPROBE_ID="<id>". Includes only public headers, like a real plugin. */
#include "sco_api.h"
#include "sco_widgets.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PROBE_RELEASE_OWN      0  /* provides PROBE_ID ".svc", then withdraws it: passes */
#define PROBE_RELEASE_BORROWED 1  /* queries sco.widgets, then calls release_service on it: fails */
#define PROBE_TAIL_UNCHECKED   2  /* calls query_service without checking api->size: fails with --short-tables */
#define PROBE_TAIL_CHECKED     3  /* checks api->size first: passes with and without --short-tables */
#define PROBE_W11_UNCHECKED    4  /* uses sco.widgets 1.1 without size checks: passes, fails with --short-tables */
#define PROBE_W11_CHECKED      5  /* checks both table sizes before 1.1 calls: passes with and without */
#define PROBE_NO_RESOURCE      6
#define PROBE_COMMAND_FAILED   7
#define PROBE_COMMAND_BAD_ARG  8
#define PROBE_COMMAND_UNKNOWN  9
#define PROBE_OBSERVE_MISUSE  10
#define PROBE_W13_CHECKED     11

#ifndef PROBE_MODE
#error "build with -DPROBE_MODE=<n>"
#endif
#ifndef PROBE_ID
#define PROBE_ID "probe"
#endif

/* docs/contracts.md: the table covers the whole pointer before it is read. */
#define COVERS(api, member) ((api)->size >= offsetof(sco_api, member) + sizeof((api)->member))
#define W_COVERS(w, member) \
    ((w)->size >= offsetof(sco_widgets_v1, member) + sizeof((w)->member) && (w)->member)

static const sco_plugin_info g_info = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, PROBE_ID, "1.0.0", "sco-core tests"
};

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &g_info; }

#if PROBE_MODE >= PROBE_NO_RESOURCE && PROBE_MODE <= PROBE_OBSERVE_MISUSE
static sco_result EmptyResource(const sco_arg* args, uint32_t count, void* ctx, char* reply, uint32_t capacity) {
    (void)args; (void)count; (void)ctx;
    if (reply && capacity) snprintf(reply, capacity, "No current synthetic job");
#if PROBE_MODE == PROBE_NO_RESOURCE
    return SCO_NOT_FOUND;
#elif PROBE_MODE == PROBE_COMMAND_FAILED
    return SCO_FAILED;
#elif PROBE_MODE == PROBE_COMMAND_BAD_ARG
    return SCO_BAD_ARG;
#elif PROBE_MODE == PROBE_COMMAND_UNKNOWN
    return (sco_result)999;
#else
    (void)((const sco_api*)ctx)->release_service(NULL, "sco.widgets");
    return SCO_NOT_FOUND;
#endif
}
#endif

#if PROBE_MODE == PROBE_RELEASE_OWN
typedef struct probe_service { uint32_t size; int (*answer)(void); } probe_service;
static int Answer(void) { return 42; }
static const probe_service g_svc = { sizeof(probe_service), Answer };
#endif

#if PROBE_MODE == PROBE_W11_UNCHECKED || PROBE_MODE == PROBE_W11_CHECKED
/* Drives every sco.widgets 1.1 member and checks the stand-in answers as the host does
 * (src/widgets/widgets.cpp). A wrong answer is logged and fails the load. */
static const sco_api* g_api;
static sco_plugin* g_self;
static int g_wrong;
static const char* const g_rows[] = { "alpha", "beta", "gamma" };

static void Expect(const char* what, sco_result got, sco_result want) {
    char line[128];
    if (got == want) return;
    snprintf(line, sizeof line, "widgets 1.1 wrong: %s returned %d, wanted %d", what, (int)got, (int)want);
    g_api->log(g_self, SCO_LOG_ERROR, line);
    g_wrong = 1;
}

static void OnEvent(const char* panel_id, const sco_widget_event* e, void* ctx) {
    char line[96];
    const int row = e->size >= sizeof(sco_widget_event_1_1) ? (int)((const sco_widget_event_1_1*)e)->row : -2;
    (void)panel_id; (void)ctx;
    snprintf(line, sizeof line, "event kind=%u size=%u row=%d", (unsigned)e->kind, (unsigned)e->size, row);
    g_api->log(g_self, SCO_LOG_INFO, line);
}

static sco_widget_node_1_1 Node11(uint32_t kind, uint32_t id, uint32_t parent, const char* label) {
    sco_widget_node_1_1 n;
    memset(&n, 0, sizeof n);
    n.base.size = sizeof n;
    n.base.kind = kind; n.base.id = id; n.base.parent = parent; n.base.label = label;
    return n;
}

static sco_result UseWidgets11(const sco_widgets_v1* w) {
    const char* panel = PROBE_ID ".panel";
    sco_widget_node_1_1 tree[4], bad[4], frag[2];
    sco_widget_node more;
    uint32_t ids[1], caps[SCO_WIDGETS_CAPS_1_1];
    char line[96];
    tree[0] = Node11(SCO_WIDGET_COLUMN, 1, 0, NULL);
    tree[1] = Node11(SCO_WIDGET_LABEL, 2, 1, "status");
    tree[1].severity = SCO_WIDGET_SEVERITY_WARNING;
    tree[1].tooltip = "probe tooltip";
    tree[2] = Node11(SCO_WIDGET_LIST, 3, 1, "rows");
    tree[2].base.items = g_rows; tree[2].base.item_count = 3;
    tree[2].first_row = 1; tree[2].visible_rows = 2;
    tree[3] = Node11(SCO_WIDGET_BUTTON, 4, 1, "go");

    memcpy(bad, tree, sizeof bad); bad[1].severity = 4;
    Expect("set_panel severity 4", w->set_panel(g_self, panel, "Probe", &bad[0].base, 4, sizeof bad[0], OnEvent, NULL), SCO_BAD_ARG);
    memcpy(bad, tree, sizeof bad); bad[1].visible_rows = 1;
    Expect("set_panel paging on a label", w->set_panel(g_self, panel, "Probe", &bad[0].base, 4, sizeof bad[0], OnEvent, NULL), SCO_BAD_ARG);
    memcpy(bad, tree, sizeof bad); bad[2].first_row = 3;
    Expect("set_panel first_row past rows", w->set_panel(g_self, panel, "Probe", &bad[0].base, 4, sizeof bad[0], OnEvent, NULL), SCO_BAD_ARG);
    memcpy(bad, tree, sizeof bad); bad[2].visible_rows = SCO_WIDGETS_MAX_ITEMS + 1u;
    Expect("set_panel visible_rows 257", w->set_panel(g_self, panel, "Probe", &bad[0].base, 4, sizeof bad[0], OnEvent, NULL), SCO_TOO_MANY);
    memcpy(bad, tree, sizeof bad); bad[3]._reserved = 1;
    Expect("set_panel _reserved", w->set_panel(g_self, panel, "Probe", &bad[0].base, 4, sizeof bad[0], OnEvent, NULL), SCO_BAD_ARG);
    Expect("set_panel", w->set_panel(g_self, panel, "Probe", &tree[0].base, 4, sizeof tree[0], OnEvent, NULL), SCO_OK);

    frag[0] = Node11(SCO_WIDGET_LABEL, 2, 1, "status (2)");
    Expect("update_nodes replace", w->update_nodes(g_self, panel, &frag[0].base, 1, sizeof frag[0]), SCO_OK);
    frag[0].base.parent = 0;
    Expect("update_nodes reparent", w->update_nodes(g_self, panel, &frag[0].base, 1, sizeof frag[0]), SCO_BAD_ARG);
    memset(&more, 0, sizeof more);   /* a 1.0-sized node at a 1.0 stride */
    more.size = sizeof more; more.kind = SCO_WIDGET_BUTTON; more.id = 5; more.parent = 1; more.label = "more";
    Expect("update_nodes append 1.0 node", w->update_nodes(g_self, panel, &more, 1, sizeof more), SCO_OK);
    frag[0] = Node11(SCO_WIDGET_ROW, 6, 1, NULL);
    frag[1] = Node11(SCO_WIDGET_LABEL, 3, 6, "taken");
    Expect("update_nodes id collision", w->update_nodes(g_self, panel, &frag[0].base, 2, sizeof frag[0]), SCO_BAD_ARG);
    frag[1] = Node11(SCO_WIDGET_LABEL, 7, 1, "sibling");
    Expect("update_nodes second root", w->update_nodes(g_self, panel, &frag[0].base, 2, sizeof frag[0]), SCO_BAD_ARG);
    frag[0] = Node11(SCO_WIDGET_LABEL, 8, 2, "under a label");
    Expect("update_nodes non-layout parent", w->update_nodes(g_self, panel, &frag[0].base, 1, sizeof frag[0]), SCO_BAD_ARG);
    Expect("update_nodes unknown panel", w->update_nodes(g_self, PROBE_ID ".none", &frag[0].base, 1, sizeof frag[0]), SCO_NOT_FOUND);

    ids[0] = 99;
    Expect("remove_nodes absent id", w->remove_nodes(g_self, panel, ids, 1), SCO_NOT_FOUND);
    ids[0] = 0;
    Expect("remove_nodes zero id", w->remove_nodes(g_self, panel, ids, 1), SCO_BAD_ARG);
    ids[0] = 1;
    Expect("remove_nodes whole panel", w->remove_nodes(g_self, panel, ids, 1), SCO_BAD_ARG);
    ids[0] = 5;
    Expect("remove_nodes", w->remove_nodes(g_self, panel, ids, 1), SCO_OK);

    {
        sco_widget_node_1_2 tagged[2];
        const char* tagged_panel = PROBE_ID ".tags";
        memset(tagged, 0, sizeof tagged);
        tagged[0].base = Node11(SCO_WIDGET_COLUMN, 10, 0, NULL);
        tagged[0].base.base.size = sizeof tagged[0];
        tagged[0].tag = "probe.root";
        tagged[1].base = Node11(SCO_WIDGET_LABEL, 11, 10, "tagged");
        tagged[1].base.base.size = sizeof tagged[1];
        tagged[1].tag = "probe.child";
        Expect("set_panel stable tags", w->set_panel(g_self, tagged_panel, "Tags", &tagged[0].base.base,
               2, sizeof tagged[0], NULL, NULL), SCO_OK);
        tagged[1].tag = "probe.root";
        Expect("set_panel duplicate tag", w->set_panel(g_self, tagged_panel, "Tags", &tagged[0].base.base,
               2, sizeof tagged[0], NULL, NULL), SCO_BAD_ARG);
        Expect("update_nodes duplicate tag", w->update_nodes(g_self, tagged_panel, &tagged[1].base.base,
               1, sizeof tagged[1]), SCO_BAD_ARG);
        tagged[1].tag = "bad tag";
        Expect("set_panel malformed tag", w->set_panel(g_self, tagged_panel, "Tags", &tagged[0].base.base,
               2, sizeof tagged[0], NULL, NULL), SCO_BAD_ARG);
        tagged[1].tag = "probe.child";
        tagged[1]._reserved[2] = 1;
        Expect("set_panel tag reserved", w->set_panel(g_self, tagged_panel, "Tags", &tagged[0].base.base,
               2, sizeof tagged[0], NULL, NULL), SCO_BAD_ARG);
    }

    caps[SCO_WIDGETS_CAP_COUNT] = 1;
    Expect("capabilities count 1", w->capabilities(g_self, caps), SCO_BAD_ARG);
    memset(caps, 0, sizeof caps);
    caps[SCO_WIDGETS_CAP_COUNT] = SCO_WIDGETS_CAPS_1_1;
    Expect("capabilities", w->capabilities(g_self, caps), SCO_OK);
    if (caps[SCO_WIDGETS_CAP_COUNT] != SCO_WIDGETS_CAPS_1_1 ||
        !(caps[SCO_WIDGETS_CAP_FEATURES] & SCO_WIDGETS_FEATURE_UPDATE_NODES)) g_wrong = 1;
    snprintf(line, sizeof line, "widgets 1.1 %s features=0x%02x", g_wrong ? "wrong" : "ok",
             (unsigned)caps[SCO_WIDGETS_CAP_FEATURES]);
    g_api->log(g_self, SCO_LOG_INFO, line);
    return g_wrong ? SCO_FAILED : SCO_OK;
}
#endif

#if PROBE_MODE == PROBE_W13_CHECKED
static const sco_api* numeric_api;
static sco_plugin* numeric_self;
static void NumericEvent(const char* panel, const sco_widget_event* event, void* context) {
    (void)panel; (void)context;
    numeric_api->log(numeric_self, event && event->value == 58 ? SCO_LOG_INFO : SCO_LOG_ERROR,
                     event && event->value == 58 ? "numeric integer event 58" : "invalid numeric event");
}
#endif

SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
#if PROBE_MODE >= PROBE_NO_RESOURCE && PROBE_MODE <= PROBE_OBSERVE_MISUSE
    const sco_command command = { sizeof(sco_command), 0, PROBE_ID ".progress", "Progress",
        "Read a current synthetic job", NULL, NULL, 0, sizeof(sco_arg_def), EmptyResource, (void*)api };
    return api->register_command(self, &command);
#elif PROBE_MODE == PROBE_W13_CHECKED
    const void* table = NULL;
    const sco_widgets_v1* widgets;
    sco_widget_node_1_3 node;
    uint32_t caps[SCO_WIDGETS_CAPS_1_2] = {SCO_WIDGETS_CAPS_1_2};
    if (!COVERS(api, query_service) || !api->query_service) {
        api->log(self, SCO_LOG_INFO, "1.0 host: no services");
        return SCO_OK;
    }
    if (api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_3, &table) != SCO_OK || !table)
        return SCO_UNAVAILABLE;
    widgets = (const sco_widgets_v1*)table;
    if (!W_COVERS(widgets, set_panel) || !W_COVERS(widgets, capabilities) ||
        widgets->capabilities(self, caps) != SCO_OK ||
        !(caps[SCO_WIDGETS_CAP_FEATURES] & SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION))
        return SCO_UNAVAILABLE;
    memset(&node, 0, sizeof node);
    node.base.base.base.size = sizeof node;
    node.base.base.base.kind = SCO_WIDGET_SLIDER;
    node.base.base.base.id = 1;
    node.base.base.base.label = "Darkness";
    node.base.base.base.minimum = 20; node.base.base.base.maximum = 95; node.base.base.base.value = 74;
    node.presentation = SCO_WIDGET_NUMERIC_INTEGER_PERCENT;
    node._reserved[0] = 1;
    if (widgets->set_panel(self, PROBE_ID ".panel", "Numeric", &node.base.base.base, 1, sizeof node, NULL, NULL) != SCO_BAD_ARG)
        return SCO_FAILED;
    node._reserved[0] = 0; node.base.base.base.value = 74.5;
    if (widgets->set_panel(self, PROBE_ID ".panel", "Numeric", &node.base.base.base, 1, sizeof node, NULL, NULL) != SCO_BAD_ARG)
        return SCO_FAILED;
    node.base.base.base.value = 74; numeric_api = api; numeric_self = self;
    return widgets->set_panel(self, PROBE_ID ".panel", "Numeric", &node.base.base.base, 1, sizeof node, NumericEvent, NULL);
#elif PROBE_MODE == PROBE_RELEASE_OWN
    sco_result r;
    if (!COVERS(api, release_service)) return SCO_OK;
    r = api->provide_service(self, PROBE_ID ".svc", 1u << 16, &g_svc);
    if (r != SCO_OK) return r;
    return api->release_service(self, PROBE_ID ".svc");
#elif PROBE_MODE == PROBE_RELEASE_BORROWED
    const void* table = NULL;
    if (!COVERS(api, release_service)) return SCO_OK;
    if (api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) != SCO_OK) return SCO_FAILED;
    (void)api->release_service(self, SCO_WIDGETS_NAME);   /* the misuse: the table is borrowed */
    return SCO_OK;
#elif PROBE_MODE == PROBE_TAIL_UNCHECKED
    const void* table = NULL;
    const sco_result r = api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table);   /* no size check */
    api->log(self, SCO_LOG_INFO, r == SCO_OK ? "widgets found" : "widgets missing");
    return SCO_OK;
#elif PROBE_MODE == PROBE_TAIL_CHECKED
    const void* table = NULL;
    if (!COVERS(api, query_service)) {
        api->log(self, SCO_LOG_INFO, "1.0 host: no services");
        return SCO_OK;
    }
    api->log(self, SCO_LOG_INFO,
             api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) == SCO_OK ? "widgets found"
                                                                                           : "widgets missing");
    return SCO_OK;
#elif PROBE_MODE == PROBE_W11_UNCHECKED
    const void* table = NULL;
    g_api = api; g_self = self;
    if (api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) != SCO_OK || !table) {   /* no size check */
        api->log(self, SCO_LOG_INFO, "widgets missing");
        return SCO_OK;
    }
    return UseWidgets11((const sco_widgets_v1*)table);   /* no widgets size check either */
#elif PROBE_MODE == PROBE_W11_CHECKED
    const void* table = NULL;
    const sco_widgets_v1* w;
    g_api = api; g_self = self;
    if (!COVERS(api, query_service)) {
        api->log(self, SCO_LOG_INFO, "1.0 host: no services");
        return SCO_OK;
    }
    if (api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) != SCO_OK || !table) {
        api->log(self, SCO_LOG_INFO, "widgets missing");
        return SCO_OK;
    }
    w = (const sco_widgets_v1*)table;
    if (!W_COVERS(w, update_nodes) || !W_COVERS(w, remove_nodes) || !W_COVERS(w, capabilities)) {
        api->log(self, SCO_LOG_INFO, "widgets 1.0 table: set_panel only");
        return SCO_OK;
    }
    return UseWidgets11(w);
#else
#error "unknown PROBE_MODE"
#endif
}

SCO_EXPORT void sco_plugin_unload(void) {}
