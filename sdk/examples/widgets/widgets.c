/* SPDX-License-Identifier: GPL-3.0-only
 * Renderer-neutral example: synthetic data only; no renderer headers or linkage.
 * The host copies the tree. Events update our model; set_panel replaces it whole.
 */
#include "sco_widgets.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const sco_api* api;
static sco_plugin* me;
static const sco_widgets_v1* widgets;
static unsigned events;
static const sco_plugin_info info = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
    "widgets", "1.0.0", "scubamount"
};
static sco_result ping(const sco_arg* args, uint32_t count, void* ctx,
                       char* reply, uint32_t capacity) {
    (void)args; (void)ctx;
    if (count != 0) return SCO_BAD_ARG;
    if (reply && capacity) snprintf(reply, capacity, "widget command");
    return SCO_OK;
}
static void invoked(sco_result result, const char* reply, void* ctx) {
    (void)ctx;
    if (result == SCO_OK && reply) api->log(me, SCO_LOG_INFO, reply);
}
static void on_event(const char* panel, const sco_widget_event* event, void* ctx) {
    char message[128];
    (void)ctx;
    if (!event || event->size < sizeof(*event)) return;
    ++events;
    snprintf(message, sizeof message, "widgets: event %u node %u kind %u in %s",
             events, (unsigned)event->node_id, (unsigned)event->kind, panel);
    api->log(me, SCO_LOG_INFO, message);
    /* Commands keep their own validation/guard, even inside an event callback. */
    if (event->kind == SCO_WIDGET_BUTTON) {
        (void)api->invoke(me, "widgets.ping", NULL, 0, invoked, NULL);
    }
}
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &info; }
SCO_EXPORT sco_result sco_plugin_load(const sco_api* host, sco_plugin* self) {
    const void* service = NULL;
    sco_widget_node nodes[10];
    const char* choices[] = {"Example Alpha", "Example Beta"};
    const char* cells[] = {"Name", "Value", "Example Alpha", "42"};
    sco_command command;
    sco_result r;
    api = host; me = self; widgets = NULL; events = 0;
    memset(&command, 0, sizeof command);
    command.size = sizeof command;
    command.name = "widgets.ping";
    command.title = "Widget command";
    command.arg_def_size = sizeof(sco_arg_def);
    command.fn = ping;
    r = api->register_command(me, &command);
    if (r != SCO_OK) return r;
    /* Missing optional UI is not a failed plugin: its command still works. */
    if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service) ||
        !api->query_service ||
        api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &service) != SCO_OK ||
        !service) return SCO_OK;
    widgets = (const sco_widgets_v1*)service;
    if (widgets->size < offsetof(sco_widgets_v1, remove_panel) + sizeof(widgets->remove_panel) ||
        !widgets->set_panel || !widgets->remove_panel) { widgets = NULL; return SCO_OK; }
    memset(nodes, 0, sizeof nodes);
    for (unsigned i = 0; i < 10; ++i) { nodes[i].size = sizeof(nodes[i]); nodes[i].id = i + 1; }
    nodes[0].kind = SCO_WIDGET_COLUMN;
    nodes[1].kind = SCO_WIDGET_LABEL; nodes[1].parent = 1; nodes[1].label = "Synthetic widget demo";
    nodes[2].kind = SCO_WIDGET_ROW; nodes[2].parent = 1;
    nodes[3].kind = SCO_WIDGET_BUTTON; nodes[3].parent = 3; nodes[3].label = "Run command";
    nodes[4].kind = SCO_WIDGET_TOGGLE; nodes[4].parent = 3; nodes[4].label = "Enabled";
    nodes[5].kind = SCO_WIDGET_SLIDER; nodes[5].parent = 1; nodes[5].label = "Amount";
    nodes[5].value = 0.5; nodes[5].minimum = 0; nodes[5].maximum = 1;
    nodes[6].kind = SCO_WIDGET_COMBO; nodes[6].parent = 1; nodes[6].label = "Choice";
    nodes[6].items = choices; nodes[6].item_count = 2;
    nodes[7].kind = SCO_WIDGET_LIST; nodes[7].parent = 1; nodes[7].label = "Items";
    nodes[7].items = choices; nodes[7].item_count = 2; nodes[7].index = -1;
    nodes[8].kind = SCO_WIDGET_TEXT_INPUT; nodes[8].parent = 1; nodes[8].label = "Note";
    nodes[8].text = "Example text";
    nodes[9].kind = SCO_WIDGET_TABLE; nodes[9].parent = 1; nodes[9].label = "Values";
    nodes[9].items = cells; nodes[9].item_count = 4; nodes[9].columns = 2;
    return widgets->set_panel(me, "widgets.main", "Widgets", nodes, 10,
                              sizeof(nodes[0]), on_event, NULL);
}
SCO_EXPORT void sco_plugin_unload(void) {
    if (widgets) (void)widgets->remove_panel(me, "widgets.main");
    widgets = NULL;
}
