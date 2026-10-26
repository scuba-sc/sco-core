/* SPDX-License-Identifier: GPL-3.0-only
 * Compile-only independent layout/signature pins for sco.widgets v1.0.
 * C11, C++20, short enums, and freestanding MSVC x64 target.
 */
#include <stddef.h>
#include "sco_widgets.h"
#ifdef __cplusplus
#define PIN(e) static_assert(e, #e)
#define ALIGN(t) alignof(t)
#else
#define PIN(e) _Static_assert(e, #e)
#define ALIGN(t) _Alignof(t)
#endif
#define AT(t, f, n) PIN(offsetof(t, f) == (n))
PIN(sizeof(void*) == 8);
PIN(SCO_WIDGETS_VERSION_1_0 == 0x00010000u);
PIN(sizeof(SCO_WIDGETS_NAME) == 12);
PIN(SCO_WIDGETS_MAX_PANELS == 64u);
PIN(SCO_WIDGETS_MAX_NODES == 256u);
PIN(SCO_WIDGETS_MAX_TOTAL_NODES == 4096u);
PIN(SCO_WIDGETS_MAX_ITEMS == 256u);
PIN(SCO_WIDGETS_MAX_COLUMNS == 16u);
PIN(SCO_WIDGETS_MAX_TEXT == 255u);
PIN(SCO_WIDGETS_MAX_ID == 63u);
PIN(SCO_WIDGETS_MAX_TITLE == 63u);
PIN(SCO_WIDGETS_MAX_EVENTS == 256u);
PIN(SCO_WIDGET_LABEL == 1u);
PIN(SCO_WIDGET_BUTTON == 2u);
PIN(SCO_WIDGET_TOGGLE == 3u);
PIN(SCO_WIDGET_SLIDER == 4u);
PIN(SCO_WIDGET_COMBO == 5u);
PIN(SCO_WIDGET_LIST == 6u);
PIN(SCO_WIDGET_TEXT_INPUT == 7u);
PIN(SCO_WIDGET_TABLE == 8u);
PIN(SCO_WIDGET_ROW == 9u);
PIN(SCO_WIDGET_COLUMN == 10u);
PIN(SCO_WIDGET_DISABLED == 1u);
PIN(sizeof(sco_widget_node) == 80);
PIN(ALIGN(sco_widget_node) == 8);
AT(sco_widget_node, size, 0);
AT(sco_widget_node, kind, 4);
AT(sco_widget_node, id, 8);
AT(sco_widget_node, parent, 12);
AT(sco_widget_node, flags, 16);
AT(sco_widget_node, index, 20);
AT(sco_widget_node, label, 24);
AT(sco_widget_node, text, 32);
AT(sco_widget_node, value, 40);
AT(sco_widget_node, minimum, 48);
AT(sco_widget_node, maximum, 56);
AT(sco_widget_node, items, 64);
AT(sco_widget_node, item_count, 72);
AT(sco_widget_node, columns, 76);
PIN(sizeof(sco_widget_event) == 32);
PIN(ALIGN(sco_widget_event) == 8);
AT(sco_widget_event, size, 0);
AT(sco_widget_event, node_id, 4);
AT(sco_widget_event, kind, 8);
AT(sco_widget_event, index, 12);
AT(sco_widget_event, value, 16);
AT(sco_widget_event, text, 24);
PIN(sizeof(sco_widgets_v1) == 48);
PIN(ALIGN(sco_widgets_v1) == 8);
AT(sco_widgets_v1, size, 0);
AT(sco_widgets_v1, _pad, 4);
AT(sco_widgets_v1, set_panel, 8);
AT(sco_widgets_v1, remove_panel, 16);
static void signatures(const sco_widgets_v1* t) {
    sco_result (*set)(sco_plugin*, const char*, const char*, const sco_widget_node*, uint32_t, uint32_t, sco_widget_event_fn, void*) = t->set_panel;
    sco_result (*remove)(sco_plugin*, const char*) = t->remove_panel;
    sco_widget_event_fn event = (void (*)(const char*, const sco_widget_event*, void*))0;
    (void)set; (void)remove; (void)event;
}
void sco_abi_widgets_pins(void);
void sco_abi_widgets_pins(void) { (void)&signatures; }
/* sco.widgets 1.1: additions only. Every 1.0 pin above keeps its value except
 * the table size (24 -> 48, three tail members); the 1.0 node and event stay
 * 80 and 32 bytes, so 1.0 strides and size checks remain valid. */
PIN(SCO_WIDGETS_VERSION_1_1 == 0x00010001u);
PIN(SCO_WIDGET_SEVERITY_NONE == 0u);
PIN(SCO_WIDGET_SEVERITY_INFO == 1u);
PIN(SCO_WIDGET_SEVERITY_WARNING == 2u);
PIN(SCO_WIDGET_SEVERITY_ERROR == 3u);
PIN(SCO_WIDGET_EVENT_PAGE == 0x100u);
PIN(SCO_WIDGETS_CAP_COUNT == 0u);
PIN(SCO_WIDGETS_CAP_FEATURES == 1u);
PIN(SCO_WIDGETS_CAP_MAX_PANELS == 2u);
PIN(SCO_WIDGETS_CAP_MAX_NODES == 3u);
PIN(SCO_WIDGETS_CAP_MAX_TOTAL_NODES == 4u);
PIN(SCO_WIDGETS_CAP_MAX_ITEMS == 5u);
PIN(SCO_WIDGETS_CAP_MAX_COLUMNS == 6u);
PIN(SCO_WIDGETS_CAP_MAX_TEXT == 7u);
PIN(SCO_WIDGETS_CAP_MAX_EVENTS == 8u);
PIN(SCO_WIDGETS_CAPS_1_1 == 9u);
PIN(SCO_WIDGETS_FEATURE_UPDATE_NODES == 0x01u);
PIN(SCO_WIDGETS_FEATURE_REMOVE_NODES == 0x02u);
PIN(SCO_WIDGETS_FEATURE_SEVERITY == 0x04u);
PIN(SCO_WIDGETS_FEATURE_TOOLTIP == 0x08u);
PIN(SCO_WIDGETS_FEATURE_PAGING == 0x10u);
PIN(SCO_WIDGETS_FEATURE_EVENT_ROW == 0x20u);
PIN(SCO_WIDGETS_FEATURE_COALESCED_RENDER == 0x40u);
PIN(sizeof(sco_widget_node_1_1) == 104);
PIN(ALIGN(sco_widget_node_1_1) == 8);
AT(sco_widget_node_1_1, base, 0);
AT(sco_widget_node_1_1, tooltip, 80);
AT(sco_widget_node_1_1, severity, 88);
AT(sco_widget_node_1_1, first_row, 92);
AT(sco_widget_node_1_1, visible_rows, 96);
AT(sco_widget_node_1_1, _reserved, 100);
PIN(sizeof(sco_widget_event_1_1) == 40);
PIN(ALIGN(sco_widget_event_1_1) == 8);
AT(sco_widget_event_1_1, base, 0);
AT(sco_widget_event_1_1, row, 32);
AT(sco_widget_event_1_1, _reserved, 36);
PIN(SCO_WIDGETS_VERSION_1_2 == 0x00010002u);
PIN(SCO_WIDGETS_FEATURE_STABLE_TAGS == 0x80u);
PIN(SCO_WIDGETS_CAP_MAX_TAG == 9u);
PIN(SCO_WIDGETS_CAPS_1_2 == 10u);
PIN(SCO_WIDGETS_MAX_TAG == 63u);
PIN(sizeof(sco_widget_node_1_2) == 128);
PIN(ALIGN(sco_widget_node_1_2) == 8);
AT(sco_widget_node_1_2, base, 0);
AT(sco_widget_node_1_2, tag, 104);
AT(sco_widget_node_1_2, _reserved, 112);
PIN(SCO_WIDGETS_VERSION_1_3 == 0x00010003u);
PIN(SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION == 0x100u);
PIN(SCO_WIDGET_NUMERIC_NUMBER == 0u);
PIN(SCO_WIDGET_NUMERIC_INTEGER == 1u);
PIN(SCO_WIDGET_NUMERIC_INTEGER_PERCENT == 2u);
PIN(sizeof(sco_widget_node_1_3) == 144);
PIN(ALIGN(sco_widget_node_1_3) == 8);
AT(sco_widget_node_1_3, base, 0);
AT(sco_widget_node_1_3, presentation, 128);
AT(sco_widget_node_1_3, _reserved, 132);
AT(sco_widgets_v1, update_nodes, 24);
AT(sco_widgets_v1, remove_nodes, 32);
AT(sco_widgets_v1, capabilities, 40);
static void signatures_1_1(const sco_widgets_v1* t) {
    sco_result (*update)(sco_plugin*, const char*, const sco_widget_node*, uint32_t, uint32_t) = t->update_nodes;
    sco_result (*remove)(sco_plugin*, const char*, const uint32_t*, uint32_t) = t->remove_nodes;
    sco_result (*caps)(sco_plugin*, uint32_t*) = t->capabilities;
    (void)update; (void)remove; (void)caps;
}
void sco_abi_widgets_1_1_pins(void);
void sco_abi_widgets_1_1_pins(void) { (void)&signatures_1_1; }
