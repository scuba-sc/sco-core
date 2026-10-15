# game.build — what is running, and was it verified

`sc_game_build.h` · service `game.build` · version 1.0 · game thread only ·
capability `game.build.info` (**ON** when published).

`game.build.read` copies a small struct: the running `StarCitizen.exe`'s PE
`TimeDateStamp` and `SizeOfImage`, the game pack's version triple, the four-part
game build the pack's rows were verified on, that build's PE timestamp, and
`pin_matches` = (running timestamp == verified timestamp). No pointers, no engine
reads beyond the module header; the answer is constant for the process lifetime.

Why: a plugin that gates behavior on "this build is one we checked" currently
greps log lines. `pin_matches` is the same fact the game pack's own gate uses,
as data. The four-part string form lives in `sc_game_pack.h`
(`SCO_GAME_PACK_VERIFIED_BUILD`); the numeric macros
(`SCO_GAME_PACK_VERIFIED_BUILD_PARTS_*`) mirror it, and the service refuses to
start if the mirrors ever disagree with the strings.

The C# and Lua game.build routes are unsupported in 1.0.
