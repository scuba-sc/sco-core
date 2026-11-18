/* SPDX-License-Identifier: GPL-3.0-only
 * sco/widgets_view.h: the host-side flat reader of "sco.widgets" panels, version 1.0.
 *
 * Host-source seam, not a public SDK header: it lives in include/sco/, is not listed in the
 * plugin SDK kits and has no entry in the SDK catalogue. It is NOT a plugin service and is
 * never returned by query_service. The host obtains it through a host-only accessor and may
 * hand it to a menu renderer written in any language. No renderer type, plugin pointer or Core
 * pointer crosses this interface. Design and choices: docs/widgets-view.md.
 *
 * Rules (the rules of sco_api.h hold here too: 64-bit only, 4-byte enums, results not
 * exceptions):
 *  - All integers are little endian. Every struct is 8-byte aligned and pinned by
 *    tests/abi_widgets_view.c. Every size field is the size the producer built; a reader
 *    accepts a larger size and ignores the tail, and refuses a smaller one.
 *  - Thread rules, per function, are stated below. Reads run no plugin code, take no
 *    plugin lock and are bounded by the widget limits. dispatch_events is the only call
 *    that runs plugin callbacks.
 *  - Every call takes the Core runtime lease, as RenderPanel does, and answers
 *    SCO_UNAVAILABLE once stop has begun (menu_thread_leave excepted). The renderer's thread
 *    may still be running when the client's stop returns (the host joins it later), so a call
 *    after stop is expected and safe: it answers SCO_UNAVAILABLE.
 *  - Both the function table and the client handle are static for the life of the process.
 *    Their memory is never freed or reused. A renderer may keep either pointer past stop.
 *  - Strings are UTF-8 and NUL-terminated inside the blob. A reader must still validate
 *    offsets, lengths and UTF-8, and must not trust the blob.
 *  - Order of checks in every call: NULL client (SCO_BAD_ARG), stopped (SCO_UNAVAILABLE),
 *    thread (SCO_WRONG_THREAD), then the arguments, then the panel.
 */
#ifndef SCO_WIDGETS_VIEW_H
#define SCO_WIDGETS_VIEW_H
#include "sco_widgets.h"
#ifdef __cplusplus
extern "C" {
#endif

/* There is no service name: the table is never published by query_service. */
#define SCO_WIDGETS_VIEW_VERSION_1_0 0x00010000u

/* "SCVB" read as a little-endian u32. */
#define SCO_VIEW_BLOB_MAGIC 0x42564353u
#define SCO_VIEW_ID_BYTES 64u /* fixed id, title and owner fields, NUL included */

/* Opaque. One per host menu instance. */
typedef struct sco_view_client sco_view_client;

/* A string inside a blob: byte offset from strings_offset, and length without the NUL.
 * {0, 0} is the empty string. */
typedef struct sco_view_str {
    uint32_t offset;
    uint32_t length;
} sco_view_str;

/* ---- panel list ---- */
#define SCO_VIEW_PANEL_HOST_OWNED 0x1u    /* a panel owned by the host's own reserved identity */
#define SCO_VIEW_PANEL_TAGGED_EVENTS 0x2u /* the panel is in tagged-event mode (widgets 1.4) */
typedef struct sco_view_panel_entry {
    uint32_t size;       /* sizeof(sco_view_panel_entry) as the host built it: 224 */
    uint32_t flags;      /* SCO_VIEW_PANEL_* */
    uint64_t revision;   /* see "Counters" in the design page */
    uint64_t epoch;      /* changes only when the panel is replaced */
    uint32_t node_count;
    uint32_t _reserved;  /* zero */
    char id[SCO_VIEW_ID_BYTES];    /* panel id, NUL-terminated */
    char title[SCO_VIEW_ID_BYTES]; /* panel title, NUL-terminated */
    char owner[SCO_VIEW_ID_BYTES]; /* owning plugin id, NUL-terminated; "menu" for host panels */
} sco_view_panel_entry;

/* ---- one panel as a blob ----
 * Layout, from the start of the buffer:
 *   sco_view_blob_header                       (header.size bytes)
 *   node_count x sco_view_node                 (at nodes_offset, node_stride apart)
 *   item_total x sco_view_str                  (at items_offset)
 *   string table, strings_bytes long           (at strings_offset)
 * nodes_offset, items_offset and strings_offset are each a multiple of 8, and the three
 * ranges lie inside total_bytes without overlapping; a reader refuses the blob otherwise.
 * Nodes are in preorder, exactly the tree the plugin submitted (ids unique, parent 0 or an
 * earlier ROW/COLUMN). A node's items are item_count consecutive entries of the item table
 * starting at first_item: COMBO/LIST options, or TABLE cells row-major. All other fields
 * keep the meaning they have in sco_widget_node and its 1.1, 1.2 and 1.3 extensions.
 * The text of a SCO_WIDGET_PASSWORD node is never copied (length 0, flag kept). */
typedef struct sco_view_blob_header {
    uint32_t size;          /* sizeof(sco_view_blob_header): 96 */
    uint32_t magic;         /* SCO_VIEW_BLOB_MAGIC */
    uint32_t version;       /* SCO_WIDGETS_VIEW_VERSION_1_0 */
    uint32_t total_bytes;   /* bytes used in the buffer */
    uint64_t revision;
    uint64_t epoch;
    uint32_t node_count;
    uint32_t node_stride;   /* sizeof(sco_view_node): 120 */
    uint32_t nodes_offset;
    uint32_t items_offset;
    uint32_t item_total;
    uint32_t strings_offset;
    uint32_t strings_bytes;
    uint32_t flags;         /* SCO_VIEW_PANEL_* */
    sco_view_str panel_id;
    sco_view_str title;
    sco_view_str owner;
    uint32_t _reserved[2];  /* zero */
} sco_view_blob_header;

/* Node key. A 64-bit FNV-1a hash (offset basis 0xcbf29ce484222325, prime 0x100000001b3) that
 * Core computes in one place, over these bytes in order:
 *   a tagged node:    the byte 'T', the kind as 4 bytes little endian, the tag bytes
 *   an untagged node: the byte 'U', then kind, id and parent as 4 bytes little endian each,
 *                     then the label bytes
 * (tag and label without a NUL; a computed 0 is stored as 1). A renderer never computes a
 * key: it copies the blob node's key into the event it sends. The key names "the same
 * control" across a set_panel republish (see sco_view_event). A tag is unique within a panel
 * (widgets 1.2) and at most SCO_WIDGETS_MAX_TAG (63) bytes. */
typedef struct sco_view_node {
    uint32_t size;          /* 120 */
    uint32_t kind;          /* SCO_WIDGET_* */
    uint32_t id;
    uint32_t parent;
    uint32_t flags;         /* SCO_WIDGET_DISABLED, SCO_WIDGET_PASSWORD */
    int32_t index;
    uint32_t severity;      /* SCO_WIDGET_SEVERITY_*; 0 for a 1.0 node */
    uint32_t first_row;     /* paging, LIST and TABLE only */
    uint32_t visible_rows;  /* 0 = no paging */
    uint32_t presentation;  /* SCO_WIDGET_NUMERIC_* */
    uint32_t columns;       /* TABLE only */
    uint32_t item_count;
    uint32_t first_item;    /* index into the item table */
    uint32_t _reserved;     /* zero */
    double value;
    double minimum;
    double maximum;
    sco_view_str label;
    sco_view_str text;
    sco_view_str tooltip;
    sco_view_str tag;       /* widgets 1.2 tag, at most SCO_WIDGETS_MAX_TAG (63) bytes;
                               empty when untagged */
    uint64_t key;           /* the node key above, as of this copy */
} sco_view_node;

/* ---- one event, from the menu to a plugin ----
 * The same candidate a renderer gives RenderPanel, plus the epoch of the copy the player
 * acted on and the key of the node acted on, both copied from the blob. There is no pointer
 * for a tag: the key carries the node's identity. Matching at dispatch, against the live
 * panel of that id at the moment of dispatch:
 *  1. The panel is gone or its owner was released: dropped.
 *  2. epoch equal (no set_panel since the copy): the live node with node_id must exist, have
 *     the same kind, be enabled including its ancestors, and have a key equal to node_key.
 *     The live key is computed from the live node (a tagged node by kind and tag, an untagged
 *     one by kind, id, parent and label), so the check is the same for both. A node removed
 *     and replaced by another control under the same id (another tag, or another label or
 *     parent) cannot receive the click. update_nodes of other nodes, or a new value on this
 *     node, does not change its key and never drops the event. An update_nodes that relabels
 *     an untagged node does change its key: the event is dropped (a tag keeps a control
 *     clickable across a relabel).
 *     Residual, stated: a plugin that removes an untagged node and adds a node of the same
 *     kind, id, parent and label back between copy and dispatch gets the click; that node
 *     has the same key, so it is the same control.
 *  3. epoch different (set_panel replaced the panel after the copy): the target is found by
 *     key. First the live tagged node whose key equals node_key; failing that, the live
 *     node with id node_id whose computed key equals node_key (same kind, id, parent and
 *     label as the copy, untagged). The target must have the event's kind and be enabled
 *     including its ancestors. It receives the event with ITS id, whatever the id was in
 *     the copy. No target: dropped.
 *  4. The host then validates the rest exactly as RenderPanel does, against the live node:
 *     value in range, selection or page row in range of the live items, text <= 255 bytes
 *     and valid UTF-8. The callback tag is always copied from the live node, never taken
 *     from the menu. One event is delivered to at most one node and at most once. */
typedef struct sco_view_event {
    uint32_t size;      /* 56 */
    uint32_t node_id;   /* the node's id in the copy */
    uint32_t kind;      /* the node's kind, or SCO_WIDGET_EVENT_PAGE */
    int32_t index;      /* COMBO/LIST selection, else 0 */
    double value;       /* TOGGLE/SLIDER, else 0 */
    const char* text;   /* TEXT_INPUT only: UTF-8, NUL-terminated, copied during the call; else NULL.
                           A NULL text on a TEXT_INPUT event is the empty string.
                           The one non-relocatable field (64-bit hosts only). */
    int32_t row;        /* PAGE: the requested first row; else -1 */
    uint32_t _reserved; /* zero */
    uint64_t epoch;     /* the epoch of the blob the player acted on. 0 is invalid:
                           submit_event answers SCO_BAD_ARG (a real epoch is never 0) */
    uint64_t node_key;  /* the key of the target node in that blob. 0 is invalid
                           (a real key is never 0) */
} sco_view_event;

/* capabilities(): caps[SCO_VIEW_CAP_COUNT] is the array length on entry (>= 2) and the
 * number written on return, as in sco.widgets: the host writes min(length, the entries it
 * has) and the caller reads only the entries counted. A caller with a short array gets a
 * prefix, never an error. Size negotiation: a consumer checks table `version` and `size`,
 * blob `version` and these counts; there is no minimum-version argument. */
#define SCO_VIEW_CAP_COUNT 0u
#define SCO_VIEW_CAP_FEATURES 1u
#define SCO_VIEW_CAP_MAX_PANELS 2u
#define SCO_VIEW_CAP_EVENT_QUEUE 3u  /* slots the queue holds before submit_event answers SCO_TOO_MANY */
#define SCO_VIEW_CAP_NODE_STRIDE 4u  /* sizeof(sco_view_node) */
#define SCO_VIEW_CAP_ENTRY_STRIDE 5u /* sizeof(sco_view_panel_entry) */
#define SCO_VIEW_CAPS_1_0 6u         /* array length that covers every 1.0 index */
#define SCO_VIEW_FEATURE_TAGS 0x1u     /* blob tags are filled in (widgets 1.2 and later) */
#define SCO_VIEW_FEATURE_COALESCE 0x2u /* submit_event merges a SLIDER or TEXT_INPUT event into an
                                          adjacent queued one for the same node, keeping the last
                                          value (see submit_event) */
#define SCO_VIEW_FEATURE_HOST_PANELS 0x4u /* SCO_VIEW_PANEL_HOST_OWNED is reported */

typedef struct sco_widgets_view_v1 {
    uint32_t size;    /* sizeof(sco_widgets_view_v1): 72 */
    uint32_t version; /* SCO_WIDGETS_VIEW_VERSION_1_0. A consumer checks this and `size`.
                         Functions are only ever appended. Each append raises the minor
                         macro (SCO_WIDGETS_VIEW_VERSION_1_1, and so on) and grows `size`:
                         `version` says which contract revision the host implements, `size`
                         says which functions exist. */

    /* Any thread. See SCO_VIEW_CAP_*. SCO_BAD_ARG: NULL client or caps, or caps[0] < 2.
     * SCO_UNAVAILABLE: client stopped. */
    sco_result (*capabilities)(sco_view_client* client, uint32_t* caps);

    /* Any thread, O(1), no allocation, atomics only. Writes the counters; any pointer
     * may be NULL. All counters come from one monotonic source, so a value is never
     * reused. global_revision moves on any change of any panel or of the list.
     * list_revision moves when the set of listed panels changes (add, remove, owner
     * released) or a listed panel's title, flags or node count changes. A fault is noticed
     * when Core handles it: dispatch_events returning SCO_CRASHED, or a list_panels or
     * copy_panel call that finds the owner released; list_revision moves then.
     * event_drops is the running total of queue slots dropped by
     * dispatch_events (never reset; a slot that merged several events counts once).
     * events_done is the sequence number of the last event of the newest slot that has left
     * the queue, whether the slot was delivered or dropped. It is 0 before the first slot
     * leaves, it never decreases, and it can jump by more than 1 when a slot covers several
     * merged events. Every event with a sequence number <= events_done is finished: delivered
     * (the newest value of its slot), superseded by a merge, or dropped, and the effect of a
     * delivered event on the panel is already in that panel's revision. A caller polls this
     * every frame and does more work only when a counter moved. SCO_BAD_ARG: NULL client.
     * SCO_UNAVAILABLE: client stopped. */
    sco_result (*stamps)(sco_view_client* client, uint64_t* global_revision, uint64_t* list_revision,
                         uint64_t* event_drops, uint64_t* events_done);

    /* Menu thread registration. The thread that will call list_panels, copy_panel and
     * submit_event calls menu_thread_enter first, before it does anything else with the
     * table; Core records the calling thread (the host passes no thread id).
     * SCO_OK: registered, or this thread already is. SCO_WRONG_THREAD: the caller is the
     * game thread. SCO_FAILED: another thread is registered (one at a time; it is released by
     * its own menu_thread_leave, or by the host after it has joined that thread). SCO_BAD_ARG:
     * NULL client. SCO_UNAVAILABLE: client stopped. Never runs plugin code. This is a
     * correctness guard (it keeps panel reads and event submission to one named thread), not a
     * security boundary: native code in the process could do anything anyway, and no plugin can
     * obtain this table. */
    sco_result (*menu_thread_enter)(sco_view_client* client);

    /* Releases the registration. SCO_OK: released, or nothing was registered.
     * SCO_WRONG_THREAD: another thread is registered; nothing changes. SCO_BAD_ARG: NULL
     * client. Works after stop (the thread must be able to leave) and never answers
     * SCO_UNAVAILABLE. The menu thread calls it on every exit path, including a panic. After it,
     * list_panels, copy_panel and submit_event answer SCO_WRONG_THREAD. */
    sco_result (*menu_thread_leave)(sco_view_client* client);

    /* The registered menu thread only (any non-game thread is proposed, not promised).
     * Writes live panels in the host's listing order (registration order).
     * out/stride: stride >= sizeof(sco_view_panel_entry) and a multiple of 8 (BAD_ARG
     * otherwise), out 8-byte aligned; each written entry is zeroed over the whole stride
     * first. *inout_count is the capacity in entries on entry and the entries
     * written on return. If capacity is too small the host writes none and sets
     * *inout_count to the entries needed, with SCO_TOO_MANY. Panels whose owner was
     * released after a fault are not listed. SCO_WRONG_THREAD: the caller is not the
     * registered menu thread; nothing is written. */
    sco_result (*list_panels)(sco_view_client* client, sco_view_panel_entry* out, uint32_t stride,
                              uint32_t* inout_count);

    /* The registered menu thread only (any non-game thread is proposed, not promised).
     * Flattens one panel into out (8-byte aligned, BAD_ARG otherwise; out may be NULL only
     * when the capacity is 0, to ask for the size). The snapshot is built under the widget lock
     * only if it is dirty; the flattening itself runs outside the lock.
     * *inout_bytes is the capacity on entry and the bytes written on return; when too
     * small the host writes nothing and sets *inout_bytes to the bytes needed, with
     * SCO_TOO_MANY. The panel may change between the size answer and the next call: a
     * caller loops, and the blob it finally gets is one consistent snapshot of one
     * revision. SCO_NOT_FOUND: no such panel. SCO_CRASHED: its owner was disabled after
     * a fault. SCO_BAD_ARG: NULL or over-long id, NULL inout_bytes. SCO_WRONG_THREAD: the
     * caller is not the registered menu thread; nothing is written. */
    sco_result (*copy_panel)(sco_view_client* client, const char* panel_id, void* out,
                             uint32_t* inout_bytes);

    /* The registered menu thread only (any non-game thread is proposed, not promised).
     * Checks the shape of the event, copies it and queues it for the game thread. It
     * runs no plugin code and does not validate against the live node: that happens in
     * dispatch_events. Events are never dropped silently: a drop adds to the event_drops
     * counter of stamps. SCO_NOT_FOUND: no such panel. SCO_BAD_ARG: bad size, unknown kind
     * (only BUTTON, TOGGLE, SLIDER, COMBO, LIST, TEXT_INPUT and PAGE are events), non-finite
     * value, text over 255 bytes or not UTF-8, nonzero reserved, epoch 0 or node_key 0.
     * SCO_TOO_MANY: the queue is full and the event could not be merged (the event is not
     * queued and takes no sequence number). SCO_UNAVAILABLE: client stopped.
     * SCO_WRONG_THREAD: not the registered menu thread; the event is not queued. Returns
     * before the plugin sees the event.
     * Sequence numbers. On SCO_OK, *seq (may be NULL) receives the event's sequence number: 1
     * for the first accepted event of the client, then +1 for each accepted event, with no
     * gaps. The queue is a list of slots in first-in first-out order; every slot covers a range
     * of sequence numbers, and the ranges of consecutive slots follow each other without a gap.
     * Coalescing, at submit time. If the event is a SLIDER or TEXT_INPUT event and the newest
     * slot is still queued (dispatch_events has not taken it) and holds a SLIDER or TEXT_INPUT
     * event with the same panel id, node_id, kind, epoch and node_key, the new event is merged
     * into that slot: the slot keeps its place, takes the new value, and its range now ends at
     * *seq. node_key is part of the condition because a different key is a different control
     * (the matching rule at sco_view_event): an event with another key takes a new slot and is
     * matched on its own, and a slot never changes its key. A merge needs no free slot, so it
     * succeeds when the queue is full. Every other event takes a new slot; BUTTON, TOGGLE,
     * COMBO, LIST and PAGE events are never merged. */
    sco_result (*submit_event)(sco_view_client* client, const char* panel_id, const sco_view_event* event,
                               uint64_t* seq);

    /* Game thread only (SCO_WRONG_THREAD otherwise); not from inside a widget callback
     * (SCO_FAILED). Takes the slots that are queued when the call starts (at most
     * SCO_WIDGETS_MAX_EVENTS; later ones wait for the next call) and handles them in order.
     * Each slot is matched as described at sco_view_event: dropped when its panel is gone,
     * when no live node matches, or when the node is disabled or of another kind; otherwise it
     * is validated and delivered with the same node update, tag rule, callout guard and
     * reentrancy rules as RenderPanel. A callback that removes its panel leaves nothing to
     * match, so the remaining slots of that panel are dropped. A callback that replaces its
     * panel, or calls update_nodes or remove_nodes, does not drop later slots: each is matched
     * on its own against the live panel (by key after a replacement), and the drain is bounded
     * by the slots taken at its start. *dispatched counts slots delivered to a callback or
     * applied to a display-only panel; *dropped counts the rest; either pointer may be NULL.
     * Merged-away events count in neither. events_done of stamps advances once per slot, to the
     * end of the slot's range, after the slot's effect is in the panel revision (or after it
     * was dropped). SCO_CRASHED: at least one owner faulted (counts are still valid; remaining
     * slots of that owner are dropped, other panels continue). SCO_UNAVAILABLE: Core stopped
     * during the call; the slots not yet handled are lost with the stop and events_done stops
     * moving. */
    sco_result (*dispatch_events)(sco_view_client* client, uint32_t* dispatched, uint32_t* dropped);
} sco_widgets_view_v1;

#ifdef __cplusplus
}
#endif
#endif /* SCO_WIDGETS_VIEW_H */
