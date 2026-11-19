/* SPDX-License-Identifier: GPL-3.0-only
 * Compile-only independent layout/signature pins for the sco.widgets.view host-source table,
 * version 1.0. C11, C++20, short enums, and freestanding MSVC x64 target. The numbers are the
 * pins table of docs/widgets-view.md; a Rust mirror asserts the same ones.
 */
#include <stddef.h>
#include "sco/widgets_view.h"
#ifdef __cplusplus
#define PIN(e) static_assert(e, #e)
#define ALIGN(t) alignof(t)
#else
#define PIN(e) _Static_assert(e, #e)
#define ALIGN(t) _Alignof(t)
#endif
#define AT(t, f, n) PIN(offsetof(t, f) == (n))
PIN(sizeof(void*) == 8);
PIN(SCO_WIDGETS_VIEW_VERSION_1_0 == 0x00010000u);
PIN(SCO_VIEW_BLOB_MAGIC == 0x42564353u);
PIN(SCO_VIEW_ID_BYTES == 64u);
PIN(SCO_VIEW_ID_BYTES > SCO_WIDGETS_MAX_TAG);
PIN(SCO_VIEW_ID_BYTES > SCO_WIDGETS_MAX_ID);
PIN(SCO_VIEW_PANEL_HOST_OWNED == 0x1u);
PIN(SCO_VIEW_PANEL_TAGGED_EVENTS == 0x2u);
PIN(SCO_VIEW_CAP_COUNT == 0u);
PIN(SCO_VIEW_CAP_FEATURES == 1u);
PIN(SCO_VIEW_CAP_MAX_PANELS == 2u);
PIN(SCO_VIEW_CAP_EVENT_QUEUE == 3u);
PIN(SCO_VIEW_CAP_NODE_STRIDE == 4u);
PIN(SCO_VIEW_CAP_ENTRY_STRIDE == 5u);
PIN(SCO_VIEW_CAPS_1_0 == 6u);
PIN(SCO_VIEW_FEATURE_TAGS == 0x1u);
PIN(SCO_VIEW_FEATURE_COALESCE == 0x2u);
PIN(SCO_VIEW_FEATURE_HOST_PANELS == 0x4u);

PIN(sizeof(sco_view_str) == 8);
PIN(ALIGN(sco_view_str) == 4);
AT(sco_view_str, offset, 0);
AT(sco_view_str, length, 4);

PIN(sizeof(sco_view_panel_entry) == 224);
PIN(ALIGN(sco_view_panel_entry) == 8);
AT(sco_view_panel_entry, size, 0);
AT(sco_view_panel_entry, flags, 4);
AT(sco_view_panel_entry, revision, 8);
AT(sco_view_panel_entry, epoch, 16);
AT(sco_view_panel_entry, node_count, 24);
AT(sco_view_panel_entry, _reserved, 28);
AT(sco_view_panel_entry, id, 32);
AT(sco_view_panel_entry, title, 96);
AT(sco_view_panel_entry, owner, 160);

PIN(sizeof(sco_view_blob_header) == 96);
PIN(ALIGN(sco_view_blob_header) == 8);
AT(sco_view_blob_header, size, 0);
AT(sco_view_blob_header, magic, 4);
AT(sco_view_blob_header, version, 8);
AT(sco_view_blob_header, total_bytes, 12);
AT(sco_view_blob_header, revision, 16);
AT(sco_view_blob_header, epoch, 24);
AT(sco_view_blob_header, node_count, 32);
AT(sco_view_blob_header, node_stride, 36);
AT(sco_view_blob_header, nodes_offset, 40);
AT(sco_view_blob_header, items_offset, 44);
AT(sco_view_blob_header, item_total, 48);
AT(sco_view_blob_header, strings_offset, 52);
AT(sco_view_blob_header, strings_bytes, 56);
AT(sco_view_blob_header, flags, 60);
AT(sco_view_blob_header, panel_id, 64);
AT(sco_view_blob_header, title, 72);
AT(sco_view_blob_header, owner, 80);
AT(sco_view_blob_header, _reserved, 88);

PIN(sizeof(sco_view_node) == 120);
PIN(ALIGN(sco_view_node) == 8);
AT(sco_view_node, size, 0);
AT(sco_view_node, kind, 4);
AT(sco_view_node, id, 8);
AT(sco_view_node, parent, 12);
AT(sco_view_node, flags, 16);
AT(sco_view_node, index, 20);
AT(sco_view_node, severity, 24);
AT(sco_view_node, first_row, 28);
AT(sco_view_node, visible_rows, 32);
AT(sco_view_node, presentation, 36);
AT(sco_view_node, columns, 40);
AT(sco_view_node, item_count, 44);
AT(sco_view_node, first_item, 48);
AT(sco_view_node, _reserved, 52);
AT(sco_view_node, value, 56);
AT(sco_view_node, minimum, 64);
AT(sco_view_node, maximum, 72);
AT(sco_view_node, label, 80);
AT(sco_view_node, text, 88);
AT(sco_view_node, tooltip, 96);
AT(sco_view_node, tag, 104);
AT(sco_view_node, key, 112);
/* The blob offsets are multiples of 8 only if the strides are. */
PIN(sizeof(sco_view_blob_header) % 8 == 0);
PIN(sizeof(sco_view_node) % 8 == 0);
PIN(sizeof(sco_view_str) % 4 == 0);

PIN(sizeof(sco_view_event) == 56);
PIN(ALIGN(sco_view_event) == 8);
AT(sco_view_event, size, 0);
AT(sco_view_event, node_id, 4);
AT(sco_view_event, kind, 8);
AT(sco_view_event, index, 12);
AT(sco_view_event, value, 16);
AT(sco_view_event, text, 24);
AT(sco_view_event, row, 32);
AT(sco_view_event, _reserved, 36);
AT(sco_view_event, epoch, 40);
AT(sco_view_event, node_key, 48);

PIN(sizeof(sco_widgets_view_v1) == 72);
PIN(ALIGN(sco_widgets_view_v1) == 8);
AT(sco_widgets_view_v1, size, 0);
AT(sco_widgets_view_v1, version, 4);
AT(sco_widgets_view_v1, capabilities, 8);
AT(sco_widgets_view_v1, stamps, 16);
AT(sco_widgets_view_v1, menu_thread_enter, 24);
AT(sco_widgets_view_v1, menu_thread_leave, 32);
AT(sco_widgets_view_v1, list_panels, 40);
AT(sco_widgets_view_v1, copy_panel, 48);
AT(sco_widgets_view_v1, submit_event, 56);
AT(sco_widgets_view_v1, dispatch_events, 64);

/* Signatures: the compiler checks every parameter list through these assignments. */
static sco_result Cap(sco_view_client* c, uint32_t* caps) { (void)c; (void)caps; return SCO_OK; }
static sco_result Stamps(sco_view_client* c, uint64_t* g, uint64_t* l, uint64_t* d, uint64_t* e) {
    (void)c; (void)g; (void)l; (void)d; (void)e; return SCO_OK;
}
static sco_result Thread(sco_view_client* c) { (void)c; return SCO_OK; }
static sco_result List(sco_view_client* c, sco_view_panel_entry* o, uint32_t s, uint32_t* n) {
    (void)c; (void)o; (void)s; (void)n; return SCO_OK;
}
static sco_result Copy(sco_view_client* c, const char* id, void* o, uint32_t* n) {
    (void)c; (void)id; (void)o; (void)n; return SCO_OK;
}
static sco_result Submit(sco_view_client* c, const char* id, const sco_view_event* e, uint64_t* q) {
    (void)c; (void)id; (void)e; (void)q; return SCO_OK;
}
static sco_result Dispatch(sco_view_client* c, uint32_t* a, uint32_t* b) { (void)c; (void)a; (void)b; return SCO_OK; }
extern const sco_widgets_view_v1 abi_widgets_view_probe;
const sco_widgets_view_v1 abi_widgets_view_probe = {
    sizeof(sco_widgets_view_v1), SCO_WIDGETS_VIEW_VERSION_1_0, Cap, Stamps, Thread, Thread, List, Copy, Submit, Dispatch};
