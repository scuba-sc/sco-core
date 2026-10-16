/* Independently compiled C11/C++20 game.build layout and tail refusal fixture. */
#include <sc_game_build.h>
#include <stddef.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
#define PIN static_assert
#else
#define PIN _Static_assert
#endif
PIN(sizeof(sc_game_build_v1) == 16, "table size");
PIN(offsetof(sc_game_build_v1, read) == 8, "read offset");
PIN(sizeof(sc_game_build_info) == 48, "info size");
PIN(offsetof(sc_game_build_info, pe_timestamp) == 4, "pe_timestamp offset");
PIN(offsetof(sc_game_build_info, size_of_image) == 8, "size_of_image offset");
PIN(offsetof(sc_game_build_info, pack_major) == 12, "pack_major offset");
PIN(offsetof(sc_game_build_info, verified_build) == 24, "verified_build offset");
PIN(offsetof(sc_game_build_info, verified_pe_timestamp) == 40, "verified ts offset");
PIN(offsetof(sc_game_build_info, pin_matches) == 44, "pin_matches offset");
static sco_result read_ok(sco_plugin* p, sc_game_build_info* o) { (void)p; (void)o; return SCO_OK; }
int main(void) {
    sc_game_build_v1 full = {sizeof(full), 0, read_ok};
    sc_game_build_info bi;
    memset(&bi, 0, sizeof(bi));
    bi.size = sizeof(bi);
    assert(sc_game_build_read(&full, NULL, &bi) == SCO_OK);
    sc_game_build_v1 truncated = {offsetof(sc_game_build_v1, read), 0, read_ok};
    assert(sc_game_build_read(&truncated, NULL, &bi) == SCO_UNAVAILABLE);
    assert(sc_game_build_read(NULL, NULL, &bi) == SCO_UNAVAILABLE);
    assert(sc_game_build_read(&full, NULL, NULL) == SCO_UNAVAILABLE);
    sc_game_build_v1 nullfn = {sizeof(nullfn), 0, NULL};
    assert(sc_game_build_read(&nullfn, NULL, &bi) == SCO_UNAVAILABLE);
    return 0;
}
