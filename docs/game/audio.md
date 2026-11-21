# Optional local audio triggers

`game.audio` lets a plugin start local game audio by name. `resolve` turns a trigger name into an owner-bound token, `play` starts an isolated effect and returns a playback token, and `stop` ends one playback. Version 1.1 adds a fire-and-forget `play_once`.

Header: [`sc_audio.h`](../../include/sc_audio.h). Table version 1.1. Capabilities `game.audio.triggers` and `game.audio.play_once`, both **OFF**; see [status](status.md). Not run in game.

The service is a game-pack service, not part of the neutral kernel. The C++ wrapper is `scosdk/game/audio.hpp`. C# and Lua are unsupported. By default no native engine backend is installed.

## Ownership and lifetime

Resolve an exact printable ASCII name of 1..127 bytes with your live plugin handle.
The host issues an owner-bound trigger token; repeated resolution by that owner
reuses it. Unknown names return NOT_FOUND with zero output and start no effect.
Names are case-sensitive contract inputs; no native hash behavior is certified.
There are at most 64 trigger tokens and 64 simultaneous playback tokens globally.

Play takes your trigger token and returns a distinct playback token after the
host backend accepts an isolated effect. This is acceptance, not evidence of
audible output, nor eventual completion. A trigger is not a playback token; one
plugin cannot play or stop another plugin's tokens. Stop targets one playback,
never a global trigger. No native pointer, event address or backend key is public.

All operations are synchronous and game-thread-only. Caller strings are copied
before backend use. No plugin callbacks, asynchronous task or cross-boundary
allocation is exposed. C++ methods guard the complete function-pointer extent
and nullness, including physically truncated tables. Cached tables refuse new
operations after service stop. A still-live owner can explicitly retry `Stop`
for a retained playback under the backend's failed-stop contract. Other tokens
expire on owner unload or service stop and are not reused across restart of the
service within the process.

## Cleanup and failure

Effect quota and token tracking are reserved before the backend can start an
effect. A complete backend and registered release hook are required for admission.
While the provider is healthy, owner release stops only that owner's effects and
expires its triggers. Release during a backend call is cleaned after that call
returns, including when a one-shot call reports a fault. Cleanup also drains
releases made by stop callbacks. It stops at the first failed stop and makes no
further automatic backend calls once the cleanup failure is latched.

A backend stop failure retains its effect and context and latches new operations
unavailable. Service stop withdraws publication and attempts cleanup until the
first failure; a repeated stop or later owner release does not automatically retry
the backend. An explicit `Stop` can retry a retained token for a live owner under
the backend contract that failed stop keeps that effect and context valid. A failure
cannot clear the backing context or remove its cleanup hook. The provider and host
must remain resident until restart. A successful explicit retry does not clear the
failure latch or authorize new effects.
Trusted backend callbacks are synchronous, bounded and nonthrowing; failed play
must be failure-atomic. A violated failure-atomic result with a returned effect
is retained and latches the service rather than losing cleanup tracking.

## Evidence

`tests/test_audio.cpp` uses real host plugin handles, service publication and query,
the native C++ wrapper and runtime Release. It checks owner isolation, wrong-kind
and stale tokens, exact names, wrong thread, playback quota, owner release during
play, stop failure and retry, service restart and physically truncated tables.
`tests/abi_audio.c` pins the C11/C++ table and signatures. These are synthetic host
tests, not native audio, Windows fault-containment or audible-output checks.

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

**Native status:** the one-shot adapter exists in
[`audio_engine.cpp`](../../src/game/services/audio_engine.cpp) and resolves the three
Wwise entry points by exported name from the client's own export directory
(`?PostEvent@SoundEngine@AK@@...ExternalSourceInfo...`, `?GetIDFromString@SoundEngine@AK@@YAIPEBD@Z`,
`?IsInitialized@SoundEngine@AK@@YA_NXZ`; read from the 4.10.197.2878 LIVE client,
sha256 `ddba788f…4310c2a`). Exports are not the missing piece. The adapter refuses
to install without a grounded witness that the audio pump's shared game object is
still registered when a submission lands, so `SCO_AUDIO_NATIVE=1` resolves the
exports, logs the refusal, and the host still publishes the 1.1 table with both
capabilities OFF. Nothing is posted. A moved or renamed export fails the install
instead of guessing. `tests/test_audio_engine.cpp` drives the adapter over a fake
export table: the pinned names, the refusal, the accepted/rejected/faulted
submission mapping and the fault latch. No audio is played. Not run in game.
