/* SPDX-License-Identifier: MIT */
#include "sc_places.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Pin ABI struct sizes and field offsets (Rule 2) */
    assert(sizeof(sc_place_info) == 96);
    assert(offsetof(sc_place_info, size) == 0);
    assert(offsetof(sc_place_info, flags) == 4);
    assert(offsetof(sc_place_info, place_id) == 8);
    assert(offsetof(sc_place_info, parent_place_id) == 16);
    assert(offsetof(sc_place_info, zone_id) == 24);
    assert(offsetof(sc_place_info, name) == 32);

    assert(sizeof(sc_places_v1) == 40);
    assert(offsetof(sc_places_v1, size) == 0);
    assert(offsetof(sc_places_v1, _pad) == 4);
    assert(offsetof(sc_places_v1, count) == 8);
    assert(offsetof(sc_places_v1, by_index) == 16);
    assert(offsetof(sc_places_v1, by_name) == 24);
    assert(offsetof(sc_places_v1, by_id) == 32);

    puts("PASS abi_places");
    return 0;
}
