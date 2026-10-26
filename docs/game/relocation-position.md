# Position-only local-player relocation

Current source adds `game.relocation` 1.1 and an explicit `request_position`
method. The frozen 1.0 FullPose request, 32-byte table type, 72-byte request and
strict quaternion completion stay unchanged. Saved poses/bookmarks keep that
route. Published SDK 2.4.0 RC1 does not contain this extension.

The new 48-byte `sc_relocation_position_request` contains size, timeout, target
zone ID, three position components and the target's current observation epoch.
`sc_relocation_v1_1` preserves the three-method prefix and appends
`request_position` at offset 32, ending at byte 40 on x64. Query version 1.1 and
check the whole member extent and non-NULL pointer. Native C and raw C++ are
supported; no C#/Lua mutation route is added.

## Admission and completion

Capability `game.relocation.position` is separate from FullPose readiness. The
default native adapter supplies no position callbacks, so the new capability
stays OFF and valid requests return `SCO_UNAVAILABLE`.

On the game thread, refresh `game.spatial.zone_record` for a known target ID and
use its current nonzero epoch. This bounded observation set is neither global
loaded-zone discovery nor permission to load a container. Its unknown system ID
cannot authorize movement: the relocation backend must separately certify known
equal nonzero systems. No name, root ID or dispatch result establishes arrival.

Position components must be finite and bounded by 1e12 metres; timeout is
1–60,000 ms. Invalid size, zero zone/epoch, timeout or position returns
`SCO_BAD_ARG`. Stale observation, missing backend or unknown/unsafe authority
returns `SCO_UNAVAILABLE`, leaving the output ticket unchanged. Original live
owner, offline admission, player/session/system, fault and thread guards apply.

This is position mutation as well as position completion. Only an explicit
position apply callback is used. The provider never creates a quaternion or
falls back to FullPose or owned-entity movement. A later tick after initiation
must freshly observe finite player coordinates in the admitted target frame
within 0.05 m. It does not read or compare rotation. This requests no
reorientation; it does not certify unrelated engine/physics orientation effects.
Dispatch failure is terminal, without retry.

The epoch checks admission only. Spatial tick refresh expires it, so it is not
the pending target lifetime. The backend supplies a separate opaque nonzero
instance token that changes on replacement, reparenting or authority reset.
Identity is rechecked before dispatch and completion. A different/unknown loaded
identity fails with `SC_RELOCATION_REASON_TARGET`; stream loss waits only to
timeout, and reappearance as another instance fails. Later epoch changes alone
do not invalidate the admitted instance.

Both methods share one global pending request and 32 retained terminal tickets.
Tickets are owner-scoped and never reused. Existing status/cancel handle either
kind; completion follows the creating method. Status does not advance work.
Cancellation after initiation is not rollback. Owner release forgets pending
work before dispatch; stop clears tickets. Reentrant calls refuse and callback
faults latch until process restart.

## Host-source socket and native prerequisites

`sco_game_set_relocation_backend_v2` is a separately named, size-prefixed
host-source socket in `src/game/services/relocation.h`, not a plugin install
API. The original socket/installer keep their layout. Install once on the game
thread before start; short/incomplete structs or later replacement refuse.
Player and all three position callbacks are required. FullPose target/apply/pose
are either all present or all absent; the clock is optional. Installation attests
the callbacks as verified and retains the host's fresh admission source.

Position target observation supplies the current epoch, loaded state, known
system and independent instance token. Apply must revalidate the admitted
instance. Observe returns a fresh player position only in that frame/instance.
No native registry or token producer is invented here. Missing position
callbacks do not disable a valid existing FullPose backend.

The internal `SetEntityLocalPose(entity, pos, nullptr)` helper skips rotation
for local-frame entity movement and is used for NPC cleanup. This is not proof
of local-player authority or cross-zone transport. A native position adapter
still needs grounded transport, target lifetime and readback producers, then
exact-build acceptance. G3 named loaded-zone discovery and loading unloaded
containers remain separate work.

## Released-owner enforcement

The original provider recognized permanent host handles without checking
runtime Release. It could admit a retired caller or publish a ticket after an
admission callback released the caller, and could dispatch retired pending work.
The shared entry path now checks retirement, admission rechecks after backend
callbacks, and Tick forgets released owners before dispatch. This enforces the
existing contract for both methods; valid FullPose mutation/thresholds stay.
Each callback is followed by an owner and pending ticket/mode/lifetime check.
Tick uses copied requests, so a release hook clearing a slot cannot trigger a
later observation in a reset frame or fault the provider for a fresh owner.
Requests are copied before publishing an output ticket, including when the
ticket aliases a request field.

Portable model and actual host/provider tests use synthetic evidence for ABI,
shared quotas, epoch/instance changes, streaming, cancellation, owner release,
fault and refusal behavior. No native signature/default, product SDK pin or
data staging changes. **Not run in game.** Source/SDK checks do not grant native
or release approval.
