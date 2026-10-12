/* GPL-3.0-only. Frozen 1.0 public sco.content layout and type pins. */
#include "sco_content.h"
#include <stddef.h>
#ifdef __cplusplus
#define PIN(x) static_assert(x, #x)
#else
#define PIN(x) _Static_assert(x, #x)
#endif
PIN(sizeof(void*) == 8);
PIN(sizeof(sco_content_kind) == 4);
PIN(SCO_CONTENT_SCRIPT == 2);
PIN(SCO_CONTENT_LIST == 3);
PIN(SCO_CONTENT_VERSION_1_0 == 0x00010000u);
PIN(sizeof(sco_content_v1) == 24);
PIN(offsetof(sco_content_v1, size) == 0);
PIN(offsetof(sco_content_v1, _pad) == 4);
PIN(offsetof(sco_content_v1, item) == 8);
PIN(offsetof(sco_content_v1, read) == 16);
PIN(sizeof(sco_content_item) == 1104);
PIN(offsetof(sco_content_item, size) == 0);
PIN(offsetof(sco_content_item, kind) == 4);
PIN(offsetof(sco_content_item, id) == 8);
PIN(offsetof(sco_content_item, pack) == 16);
PIN(offsetof(sco_content_item, name) == 80);
void sco_abi_content_pins(void);
void sco_abi_content_pins(void) {
    sco_result (*item)(sco_plugin*, sco_content_kind, uint32_t, sco_content_item*) = ((sco_content_v1*)0)->item;
    sco_result (*read)(sco_plugin*, uint64_t, void*, uint32_t*) = ((sco_content_v1*)0)->read;
    (void)item; (void)read;
}
#undef PIN
