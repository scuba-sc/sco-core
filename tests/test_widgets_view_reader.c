/* SPDX-License-Identifier: GPL-3.0-only
 * A strict sco.widgets.view blob reader written in C against the header alone, the way a
 * renderer in another language would write it (no Core code, no trust in the blob). The C++
 * test feeds it every blob it builds and checks the answers. It also holds a second, independent
 * implementation of the node key, so the host's key is checked against a reader that never
 * saw the host's code. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sco/widgets_view.h"

/* Return codes: 0 ok, otherwise the first rule that failed. */
enum {
    SCV_OK = 0, SCV_SHORT = 1, SCV_HEADER = 2, SCV_RANGES = 3, SCV_NODE = 4, SCV_STRING = 5,
    SCV_ITEMS = 6, SCV_TREE = 7, SCV_KEY = 8
};

static uint64_t fnv_byte(uint64_t h, unsigned char b) { h ^= b; return h * 0x100000001b3ull; }
static uint64_t fnv_u32(uint64_t h, uint32_t v) {
    int i;
    for (i = 0; i < 4; ++i) h = fnv_byte(h, (unsigned char)(v >> (8 * i)));
    return h;
}
uint64_t scv_reference_key(uint32_t kind, uint32_t id, uint32_t parent, const char* label, size_t label_len,
                           const char* tag, size_t tag_len) {
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i;
    if (tag_len) {
        h = fnv_byte(h, 'T');
        h = fnv_u32(h, kind);
        for (i = 0; i < tag_len; ++i) h = fnv_byte(h, (unsigned char)tag[i]);
    } else {
        h = fnv_byte(h, 'U');
        h = fnv_u32(h, kind);
        h = fnv_u32(h, id);
        h = fnv_u32(h, parent);
        for (i = 0; i < label_len; ++i) h = fnv_byte(h, (unsigned char)label[i]);
    }
    return h ? h : 1;
}

static int valid_utf8(const unsigned char* s, uint32_t n) {
    uint32_t i = 0;
    while (i < n) {
        uint32_t c = s[i++], count, minimum;
        if (c == 0) return 0;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; minimum = 0x80; c &= 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; minimum = 0x800; c &= 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; minimum = 0x10000; c &= 7; }
        else return 0;
        if (n - i < count) return 0;
        while (count--) {
            unsigned next = s[i++];
            if ((next & 0xc0) != 0x80) return 0;
            c = (c << 6) | (next & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0;
    }
    return 1;
}

/* The bytes of a string, or NULL when the reference is not valid in this blob. */
const char* scv_str(const void* blob, sco_view_str s) {
    const sco_view_blob_header* h = (const sco_view_blob_header*)blob;
    const unsigned char* base = (const unsigned char*)blob + h->strings_offset;
    if (s.offset == 0 && s.length == 0) return "";
    if ((uint64_t)s.offset + s.length >= h->strings_bytes) return NULL; /* the NUL must fit too */
    if (base[s.offset + s.length] != 0) return NULL;
    if (!valid_utf8(base + s.offset, s.length)) return NULL;
    return (const char*)(base + s.offset);
}

int scv_validate(const void* blob, uint32_t bytes) {
    const unsigned char* b = (const unsigned char*)blob;
    sco_view_blob_header h;
    uint64_t nodes_end, items_end, strings_end;
    uint32_t i, seen_items = 0;
    if (!blob || (uintptr_t)blob % 8 || bytes < 96) return SCV_SHORT;
    memcpy(&h, b, sizeof h);
    if (h.size < sizeof(sco_view_blob_header) || h.magic != SCO_VIEW_BLOB_MAGIC ||
        h.version < SCO_WIDGETS_VIEW_VERSION_1_0 || h.total_bytes > bytes || h.total_bytes < h.size ||
        h.node_stride < sizeof(sco_view_node) || !h.revision || !h.epoch) return SCV_HEADER;
    if (h.nodes_offset % 8 || h.items_offset % 8 || h.strings_offset % 8) return SCV_RANGES;
    nodes_end = (uint64_t)h.nodes_offset + (uint64_t)h.node_count * h.node_stride;
    items_end = (uint64_t)h.items_offset + (uint64_t)h.item_total * sizeof(sco_view_str);
    strings_end = (uint64_t)h.strings_offset + h.strings_bytes;
    if (h.nodes_offset < h.size || nodes_end > h.items_offset || items_end > h.strings_offset ||
        strings_end > h.total_bytes) return SCV_RANGES;
    if (!scv_str(blob, h.panel_id) || !scv_str(blob, h.title) || !scv_str(blob, h.owner)) return SCV_STRING;
    for (i = 0; i < h.node_count; ++i) {
        sco_view_node n;
        const char *label, *text, *tooltip, *tag;
        uint32_t j;
        memcpy(&n, b + h.nodes_offset + (uint64_t)i * h.node_stride, sizeof n);
        if (n.size < sizeof(sco_view_node) || n.size > h.node_stride) return SCV_NODE;
        if (n.kind < SCO_WIDGET_LABEL || n.kind > SCO_WIDGET_COLUMN || !n.id) return SCV_NODE;
        label = scv_str(blob, n.label); text = scv_str(blob, n.text);
        tooltip = scv_str(blob, n.tooltip); tag = scv_str(blob, n.tag);
        if (!label || !text || !tooltip || !tag) return SCV_STRING;
        if (n.tag.length > SCO_WIDGETS_MAX_TAG) return SCV_STRING;
        if ((uint64_t)n.first_item + n.item_count > h.item_total) return SCV_ITEMS;
        if (n.first_item != seen_items) return SCV_ITEMS; /* items are laid out in node order */
        for (j = 0; j < n.item_count; ++j) {
            sco_view_str s;
            memcpy(&s, b + h.items_offset + (uint64_t)(n.first_item + j) * sizeof s, sizeof s);
            if (!scv_str(blob, s)) return SCV_ITEMS;
        }
        seen_items += n.item_count;
        /* unique ids and a parent that is an earlier ROW or COLUMN */
        {
            uint32_t k;
            int parent_ok = n.parent == 0;
            for (k = 0; k < i; ++k) {
                sco_view_node o;
                memcpy(&o, b + h.nodes_offset + (uint64_t)k * h.node_stride, sizeof o);
                if (o.id == n.id) return SCV_TREE;
                if (o.id == n.parent && (o.kind == SCO_WIDGET_ROW || o.kind == SCO_WIDGET_COLUMN)) parent_ok = 1;
            }
            if (!parent_ok) return SCV_TREE;
        }
        if (n.key == 0 ||
            n.key != scv_reference_key(n.kind, n.id, n.parent, label, n.label.length, tag, n.tag.length))
            return SCV_KEY;
    }
    if (seen_items != h.item_total) return SCV_ITEMS;
    return SCV_OK;
}

/* Copy node `index` out of a validated blob. */
int scv_node(const void* blob, uint32_t index, sco_view_node* out) {
    const sco_view_blob_header* h = (const sco_view_blob_header*)blob;
    if (index >= h->node_count) return 0;
    memcpy(out, (const unsigned char*)blob + h->nodes_offset + (uint64_t)index * h->node_stride, sizeof *out);
    return 1;
}

int scv_item(const void* blob, uint32_t index, sco_view_str* out) {
    const sco_view_blob_header* h = (const sco_view_blob_header*)blob;
    if (index >= h->item_total) return 0;
    memcpy(out, (const unsigned char*)blob + h->items_offset + (uint64_t)index * sizeof *out, sizeof *out);
    return 1;
}
