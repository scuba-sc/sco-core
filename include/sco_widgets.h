/* SPDX-License-Identifier: GPL-3.0-only
 * Renderer-neutral retained panels. Found through sco_api 1.1 query_service;
 * no renderer types or headers enter a plugin. See docs/design/widgets.md.
 * All methods and event callbacks: game thread only. Strings/arrays are copied
 * on successful set_panel; fn/ctx borrowed until remove, replacement or unload.
 * Every size/stride must cover the complete v1 struct; larger tails are ignored.
 */
#ifndef SCO_WIDGETS_H
#define SCO_WIDGETS_H
#include "sco_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SCO_WIDGETS_NAME "sco.widgets"
#define SCO_WIDGETS_VERSION_1_0 0x00010000u
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
} sco_widgets_v1;
#ifdef __cplusplus
}
#endif
#endif
