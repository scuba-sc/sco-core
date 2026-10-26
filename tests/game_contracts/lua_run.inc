// SPDX-License-Identifier: GPL-3.0-only
// Game-only test cases; included by the corresponding test when the game pack is enabled.
    TestNoAdapter();
    CHECK(sco_lua_add_extension(&sco_lua_game_extension) == SCO_OK);   // as the product does
    TestDataCore();
    TestGameActors();
    TestGameCreative();
    TestGameRules();
    TestInventory();
    TestGameWorld();
    TestGameEntities();
