/* SPDX-License-Identifier: GPL-3.0-only
 * Renderer-neutral retained panels. Found through sco_api 1.1 query_service;
 * no renderer types or headers enter a plugin. See docs/design/widgets.md.
 * All methods and event callbacks: game thread only. Strings/arrays are copied
 * on successful set_panel; fn/ctx borrowed until remove, replacement or unload.
 * Every size/stride must cover the complete v1 struct; larger tails are ignored.
 * 1.1 appends update_nodes/remove_nodes/capabilities to the table and adds
 * opt-in sco_widget_node_1_1/sco_widget_event_1_1 extensions; the 1.0 node and
 * event structs keep their 80/32-byte layouts so 1.0 strides stay valid.
 */
#ifndef SCO_WIDGETS_H
#define SCO_WIDGETS_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SCO_WIDGETS_NAME "sco.widgets"
#define SCO_WIDGETS_VERSION_1_0 0x00010000u
#define SCO_WIDGETS_VERSION_1_1 0x00010001u /* + update_nodes, remove_nodes, capabilities */
#define SCO_WIDGETS_VERSION_1_2 0x00010002u /* + stable node tags */
#define SCO_WIDGETS_VERSION_1_3 0x00010003u /* + numeric slider presentation */
#define SCO_WIDGETS_MAX_PANELS 64u
#define SCO_WIDGETS_MAX_NODES 256u
#define SCO_WIDGETS_MAX_TOTAL_NODES 4096u
#define SCO_WIDGETS_MAX_ITEMS 256u
#define SCO_WIDGETS_MAX_COLUMNS 16u
#define SCO_WIDGETS_MAX_TEXT 255u
#define SCO_WIDGETS_MAX_ID 63u
#define SCO_WIDGETS_MAX_TITLE 63u
#define SCO_WIDGETS_MAX_EVENTS 256u
/* Fixed-width discriminants, never compiler-sized enums. */
#define SCO_WIDGET_LABEL 1u
#define SCO_WIDGET_BUTTON 2u
#define SCO_WIDGET_TOGGLE 3u
#define SCO_WIDGET_SLIDER 4u
#define SCO_WIDGET_COMBO 5u
#define SCO_WIDGET_LIST 6u
#define SCO_WIDGET_TEXT_INPUT 7u
#define SCO_WIDGET_TABLE 8u
#define SCO_WIDGET_ROW 9u
#define SCO_WIDGET_COLUMN 10u
#define SCO_WIDGET_DISABLED 1u
/* TEXT_INPUT only: mask characters in the host renderer. Host still copies
 * plaintext; this is display protection, not a secret-storage guarantee. */
#define SCO_WIDGET_PASSWORD 2u
/* Preorder tree: id nonzero and unique within panel; parent 0 = root, otherwise
 * a preceding ROW/COLUMN node. Each subtree must be contiguous. Labels are
 * plain UTF-8 text, never renderer markup/format strings; interactive labels
 * must be nonempty (accessible names). IDs are independent of display labels.
 * value: toggle 0/1 or finite slider value within [minimum,maximum].
 * index: combo/list selection (-1 or valid item index). text: text input value.
 * items: combo/list options, or TABLE row-major cells (count divisible by columns).
 * Unused fields must be zero/NULL, except index which may be zero or -1.
 */
typedef struct sco_widget_node {
    uint32_t size;
    uint32_t kind;
    uint32_t id;
    uint32_t parent;
    uint32_t flags;
    int32_t index;
    const char* label;
    const char* text;
    double value;
    double minimum;
    double maximum;
    const char* const* items;
    uint32_t item_count;
    uint32_t columns;
} sco_widget_node;
/* Host-generated event, borrowed only during fn. kind matches the node kind.
 * BUTTON: no payload; TOGGLE/SLIDER: value; COMBO/LIST: index;
 * TEXT_INPUT: text. No events for label, layout or table nodes. */
typedef struct sco_widget_event {
    uint32_t size;
    uint32_t node_id;
    uint32_t kind;
    int32_t index;
    double value;
    const char* text;
} sco_widget_event;
/* ---- 1.1 extensions (opt-in; a 1.0 host ignores their tails) ----
 * Node extension: base.size = sizeof(sco_widget_node_1_1) and a stride covering
 * it. Nodes whose size is below that are read as 1.0 nodes. tooltip: optional
 * plain UTF-8 <=255 bytes. severity: SCO_WIDGET_SEVERITY_*. Paging applies to
 * LIST (rows = items) and TABLE (rows = item_count / columns) only:
 * visible_rows 0 = no paging (first_row must be 0); otherwise
 * 1..SCO_WIDGETS_MAX_ITEMS rows shown from first_row, which is 0 or < rows.
 * _reserved and paging fields of other kinds must be zero. */
#define SCO_WIDGET_SEVERITY_NONE 0u
#define SCO_WIDGET_SEVERITY_INFO 1u
#define SCO_WIDGET_SEVERITY_WARNING 2u
#define SCO_WIDGET_SEVERITY_ERROR 3u
typedef struct sco_widget_node_1_1 {
    sco_widget_node base;
    const char* tooltip;
    uint32_t severity;
    uint32_t first_row;
    uint32_t visible_rows;
    uint32_t _reserved;
} sco_widget_node_1_1;
/* Paging event of a paged LIST/TABLE: event kind (not a node kind), new first
 * visible row in row; index/value/text are zero/NULL. Never sent to 1.0 panels. */
#define SCO_WIDGET_EVENT_PAGE 0x100u
/* Event extension, sent instead of a bare 1.0 event only for panels holding at
 * least one 1.1-sized node; base.size = sizeof(sco_widget_event_1_1). Check
 * event->size >= sizeof(sco_widget_event_1_1) before casting. row: PAGE = new
 * first visible row; LIST = selected row (the index); otherwise -1. */
typedef struct sco_widget_event_1_1 {
    sco_widget_event base;
    int32_t row;
    uint32_t _reserved;
} sco_widget_event_1_1;
/* ---- 1.2 extensions (opt-in; a 1.0/1.1 host ignores the tails) ----
 * Node extension: base.base.size = sizeof(sco_widget_node_1_2) and a stride
 * covering it. Nodes smaller than that are read as 1.0/1.1 (no tag). tag:
 * stable automation identifier for the node, [A-Za-z0-9._-]{1..63},
 * NULL/empty = untagged. Tags are unique within the panel: a set/update whose
 * resulting tree would carry two nodes with the same non-empty tag is
 * BAD_ARG and leaves the panel untouched. Tags are host metadata only:
 * adapters never render them and event routing stays numeric by node id.
 * _reserved must be zero. Check node->size >= sizeof(sco_widget_node_1_2)
 * before casting, after the 1.1 check.
 * capabilities: SCO_WIDGETS_CAP_MAX_TAG is the host's tag byte limit.
 */
typedef struct sco_widget_node_1_2 {
    sco_widget_node_1_1 base;
    const char* tag;
    uint32_t _reserved[3];
} sco_widget_node_1_2;
/* 1.3 appends a presentation mode without changing earlier node prefixes.
 * INTEGER and INTEGER_PERCENT apply to sliders with integral signed-32-bit
 * minimum, maximum and value; events carry integral values. NUMBER preserves
 * the existing finite-double behavior. Unknown modes and reserved words fail. */
#define SCO_WIDGET_NUMERIC_NUMBER 0u
#define SCO_WIDGET_NUMERIC_INTEGER 1u
#define SCO_WIDGET_NUMERIC_INTEGER_PERCENT 2u
typedef struct sco_widget_node_1_3 {
    sco_widget_node_1_2 base;
    uint32_t presentation;
    uint32_t _reserved[3];
} sco_widget_node_1_3;
/* capabilities(self, caps): caps[SCO_WIDGETS_CAP_COUNT] is the array length on
 * entry (>= 2) and the number of elements written on return; the host fills
 * the indices below that it knows, up to that length. Limits are the running
 * host's, which may exceed the 1.0 macros above. */
#define SCO_WIDGETS_CAP_COUNT 0u
#define SCO_WIDGETS_CAP_FEATURES 1u
#define SCO_WIDGETS_CAP_MAX_PANELS 2u
#define SCO_WIDGETS_CAP_MAX_NODES 3u
#define SCO_WIDGETS_CAP_MAX_TOTAL_NODES 4u
#define SCO_WIDGETS_CAP_MAX_ITEMS 5u
#define SCO_WIDGETS_CAP_MAX_COLUMNS 6u
#define SCO_WIDGETS_CAP_MAX_TEXT 7u
#define SCO_WIDGETS_CAP_MAX_EVENTS 8u
#define SCO_WIDGETS_CAP_MAX_TAG 9u /* longest accepted tag, in bytes */
#define SCO_WIDGETS_MAX_TAG 63u
#define SCO_WIDGETS_CAPS_1_1 9u /* array length that covers every 1.1 index */
#define SCO_WIDGETS_CAPS_1_2 10u /* array length that covers every 1.2 index */
#define SCO_WIDGETS_FEATURE_UPDATE_NODES 0x01u
#define SCO_WIDGETS_FEATURE_REMOVE_NODES 0x02u
#define SCO_WIDGETS_FEATURE_SEVERITY 0x04u
#define SCO_WIDGETS_FEATURE_TOOLTIP 0x08u
#define SCO_WIDGETS_FEATURE_PAGING 0x10u
#define SCO_WIDGETS_FEATURE_EVENT_ROW 0x20u
#define SCO_WIDGETS_FEATURE_COALESCED_RENDER 0x40u /* <= 1 snapshot rebuild per panel render pass */
#define SCO_WIDGETS_FEATURE_STABLE_TAGS 0x80u    /* sco_widget_node_1_2 tag field */
#define SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION 0x100u /* sco_widget_node_1_3 */
typedef void (*sco_widget_event_fn)(const char* panel_id, const sco_widget_event* event, void* ctx);
typedef struct sco_widgets_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Atomically adds/replaces own panel. id: <plugin-id>.<name>, [a-z0-9_.],
     * no empty dotted segments, <=63 bytes; title 1..63 bytes. count 1..256.
     * node_stride >= sizeof(sco_widget_node), aligned to its alignment, each
     * node.size >= sizeof(sco_widget_node) and <= stride. fn may be NULL for
     * display-only panels. BAD_ARG: malformed tree/data/self; TOO_MANY: limits
     * or allocation failure (ID/title length is BAD_ARG, node text/item lengths
     * are TOO_MANY). Failure leaves previous panel/callback untouched.
     * UNAVAILABLE: stopped service/host; WRONG_THREAD: not game thread.
     * fn runs under the owner's callout guard, may replace/remove its panel or
     * invoke commands; new trees render next pass. A fault stops further events.
     */
    sco_result (*set_panel)(sco_plugin* self, const char* id, const char* title,
                             const sco_widget_node* nodes, uint32_t count, uint32_t node_stride,
                             sco_widget_event_fn fn, void* ctx);
    /* Removes own panel; NOT_FOUND when absent. BAD_ARG for
     * invalid self/id, UNAVAILABLE after stop, WRONG_THREAD off game thread.
     * On success callback ctx may be freed immediately, including inside fn. */
    sco_result (*remove_panel)(sco_plugin* self, const char* id);
    /* ---- 1.1 tail: check size covers the member and it is non-NULL ---- */
    /* Atomically replaces or inserts one subtree of an own panel. nodes[0] is
     * the subtree root; every later node descends from it (preorder, same node
     * rules and stride rules as set_panel, 1.0 or 1.1 node sizes). An existing
     * root id is replaced in place and must keep its parent; a new root id is
     * appended as the last child of its parent (0 or an existing ROW/COLUMN).
     * IDs must not collide with nodes outside the replaced subtree. NOT_FOUND:
     * no such own panel. Limits/errors as set_panel; callback unchanged. The
     * change renders next pass; remaining events of the current pass drop. */
    sco_result (*update_nodes)(sco_plugin* self, const char* panel_id,
                               const sco_widget_node* nodes, uint32_t count, uint32_t node_stride);
    /* Atomically removes each listed node with its subtree (1..256 ids).
     * NOT_FOUND: no such own panel or an absent id; BAD_ARG: zero id or a
     * removal that would leave the panel empty (use remove_panel). */
    sco_result (*remove_nodes)(sco_plugin* self, const char* panel_id,
                               const uint32_t* node_ids, uint32_t count);
    /* See SCO_WIDGETS_CAP_*. BAD_ARG: invalid self, NULL caps or caps[0] < 2. */
    sco_result (*capabilities)(sco_plugin* self, uint32_t* caps);
} sco_widgets_v1;
#ifdef __cplusplus
}
#endif
#endif
