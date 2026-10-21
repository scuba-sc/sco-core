# Optional local audio triggers

`game.audio`1.0 is a game-pack service in `sc_audio.h`, with a native C++ wrapper
in `scosdk/game/audio.hpp`. It is not part of the neutral kernel. C# and Lua are
unsupported. No native engine backend is installed; production capability
`game.audio.triggers` is OFF. Not run in game.

## Ownership and lifetime

Resolve an exact printable ASCII name of1..127bytes with your live plugin handle.
The host issues an owner-bound trigger token; repeated resolution by that owner
reuses it. Unknown names return NOT_FOUND with zero output and start no effect.
Names are case-sensitive contract inputs; no native hash behavior is certified.
There are at most64 trigger tokens and64 simultaneous playback tokens globally.

Play takes your trigger token and returns a distinct playback token after the
host backend accepts an isolated effect. This is acceptance, not evidence of
audible output, nor eventual completion. A trigger is not a playback token; one
plugin cannot play or stop another plugin's tokens. Stop targets one playback,
never a global trigger. No native pointer, event address or backend key is public.

All operations are synchronous and game-thread-only. Caller strings are copied
before backend use. No plugin callbacks, asynchronous task or cross-boundary
allocation is exposed. C++ methods guard the complete function-pointer extent
and nullness, including physically truncated tables. Cached tables refuse after
service stop. Tokens expire on owner unload or service stop and are not reused
across restart of the service within the process.

## Cleanup and failure

Effect quota and token tracking are reserved before the backend can start an
effect. A complete backend and registered release hook are required for admission.
Owner release stops only that owner's effects and expires its triggers. Release
during a backend call is cleaned immediately after that call returns, before any
successful playback token can be returned to the released caller.

A backend stop failure retains its effect and context for retry and latches new
operations unavailable. Stop can retry a retained token. Service stop withdraws
publication and attempts all cleanup; failure cannot clear backing context or
remove its cleanup hook. The provider and host must remain resident until restart.
A successful later retry does not clear the failure latch or authorize new effects.
Trusted backend callbacks are synchronous, bounded and nonthrowing; failed play
must be failure-atomic. A violated failure-atomic result with a returned effect
is retained and latches the service rather than losing cleanup tracking.

## Evidence

`tests/test_audio.cpp` uses real host plugin handles, service publication/query,
the native C++ wrapper and runtime Release. It checks owner isolation, wrong-kind
and stale tokens, exact names, wrong thread, playback quota, owner release during
play, stop failure/retry, service restart and physical partial-pointer tables.
The four-participant game cleanup dispatcher is exercised with distinct callback
effects, including rejection of a fifth participant. `tests/abi_audio.c` pins
the C11/C++ table and signatures. These are synthetic host tests, not native audio,
Windows fault-containment, audible output or game verification.
## v1.1: play_once

`game.audio` 1.1 appends one member, `play_once(self, trigger)`, after `stop`.
The 1.0 prefix (`resolve` at 8, `play` at 16, `stop` at 24) is unchanged; the
table grows from 32 to 40 bytes. Callers use `sc_audio_play_once`, or
`Audio::PlayOnce` in C++, which checks the table extent first and returns
`UNAVAILABLE` against a 1.0-sized table.

`play_once` is fire-and-forget. `OK` means only that the backend accepted the
submission. It returns no playback token, gives no audible-output or completion
guarantee and has no stop. If the caller is released while the backend call is
in progress, the call can report `UNAVAILABLE` after the backend already
accepted it; nothing is rolled back or retried.

It is gated by its own capability, `game.audio.play_once` (default OFF),
independently of `game.audio.triggers`. A provider may supply `resolve` and
`play_once` without `play`/`stop`; `resolve` then issues tokens for one-shot use.
A backend fault (`CRASHED`) turns `game.audio.play_once` off until the process
restarts, surviving a service stop and start, and leaves a healthy
`game.audio.triggers` mode working, including resolving new names.

**Native status:** no native backend ships in this version. The host publishes
the 1.1 table with `game.audio.play_once` OFF. A native backend is held until it
can confirm the shared game object is still registered when the sound is
submitted; that check is not yet established. Synthetic provider tests only;
not run in game.
