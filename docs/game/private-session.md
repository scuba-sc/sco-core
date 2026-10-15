# Private-session broker (`game.private_session` 1.0)

G13 is approved **ON by default for R1**. This authorizes a host-owned broker, not
startup networking or plugin sockets. `sco_api` and `sco_net_v1` are unchanged.
External multiplayer plugins query [`sc_private_session.h`](../../include/sc_private_session.h)
using `SC_PRIVATE_SESSION_NAME` and `SC_PRIVATE_SESSION_VERSION`.

## Public request contract

All calls run on the game thread. Host/Join/Leave require a live `sco_plugin*`.
Every request and output starts with `uint32_t size`; callers set the complete v1
extent, reserved padding to zero, and check complete table member extents before
calling. New tails may be appended without changing v1 members. Strings are
borrowed for the call, lengths in bytes excluding terminators; embedded NUL is
refused. Nothing from a request is stored in settings or broker history.

- `request_host(self, host_request*)`: consent, nonzero port (default 64091),
  passphrase 12–256 bytes, player name 1–64 bytes, peer limit 2–51 including self (a runtime without `SCO_NET_SESSION_MAX_PEERS` stops at 16 and answers `SCO_BAD_ARG` above it).
- `request_join(self, join_request*)`: consent, exact numeric IPv4 host address
  (1–15 bytes), nonzero port, passphrase and player name with the same bounds.
- `leave(self)`: the requesting owner only; no consent needed to stop. Ordinary
  owner unload/release also leaves. No automatic reconnect or replacement.
- `get_state(state*)`: copied scalar Idle/Hosting/Joining/Connected state, peer
  count, configured host maximum (zero for a joiner, whose maximum is unknown).

`consent=1` means **fresh explicit local UI Host/Join activation**. Never replay it
from plugin load, saved settings, peers, commands without a local UI confirmation,
or an automatic retry. Native plugins are trusted in-process code, not sandboxed;
the broker cannot authenticate a physical button press. The multiplayer UI must
clear its passphrase on unload/leave and must not persist or log it.

Results: `OK` means backend operation accepted, not a completed authenticated join;
`BAD_ARG` covers malformed extents/strings/bounds/address policy; `FAILED` absent
consent or backend socket failure; `UNAVAILABLE` covers missing/disabled broker,
unsafe/reentrant call, failed admission or another active owner/session;
`TOO_MANY` covers four Host/Join attempts per second globally. Wrong thread is
`WRONG_THREAD`; released/bogus owner is `BAD_ARG`. Observe `net.state` and the
existing `sco.net` data service for asynchronous authentication/session outcomes.

## Host-only policy and composition

`Platform.privateSession` contains `sco::net::ControlPolicyV1`:

```cpp
// Default-on broker; no listener at Start:
policy.enabled = true;
policy.bindAddress = "127.0.0.1";
policy.allowedAddresses = { "127.0.0.1" };
```

Explicit LAN requires an exact numeric private local bind and 1–16 unique exact
private peer addresses. Private means RFC1918, loopback, link-local or chosen
CGNAT VPN (`100.64.0.0/10`); no wildcard, public address, CIDR, DNS or IPv6.
Loopback bind rejects non-loopback peers rather than silently expanding scope.
The two limits are separate: the allow-list (1–16 addresses) names who may reach the host, the peer
limit (2–51) counts the players in the session.
No public request can alter bind, allowlist or transport. Both UDP platform
implementations bind the configured interface exactly. Synthetic host tests may
inject a non-owned transport, but must obey the same numeric/scope policy.

`sco::app::Start` starts the inert net service, then publishes the broker before
plugins load. `Platform.privateSessionAdmitted` must positively certify offline
session, absent anti-cheat, blocked official network and correct target; null,
unknown and exceptions refuse publication/requests. `sco::game::host::Start`
automatically wires its existing admission inspector. Bare app hosts must supply
an equally strict inspection callback; no signature match is admission evidence.
`SCO_GAME_SC=OFF` has no public game-session service.

App ticks revoke sessions **before** queued plugin work. Game-host ticks check
admission before cadence and stop the whole host on loss. Public requests and
state reads also recheck admission. App stop closes/withdraws this consent broker
before exit/unload callbacks and before net teardown; cached process-lifetime C
tables then refuse operations. Ordinary owner release is dispatched through the
existing net release hook, not a ninth runtime hook slot. A retained-provider
quarantine can skip ordinary release, but host/admission stop still closes net.
Do not mix direct backend Host/Join calls with the broker.

## Authentication, bounds and audit

The existing backend uses PBKDF2/HMAC authenticated join and authenticated frames;
messages are **not encrypted**. Wrong passphrase, peer maximum and timeout remain
backend refusals. Session credentials live only in volatile backend session
state; no cryptographic zeroization guarantee is added. Never transmit personal
information or secrets, and use an owner-chosen private VPN for confidentiality.

- Global ingress budget: 4,096 raw datagrams/second, before framing/authentication.
- Each filtered receive examines at most 64 datagrams, bounding rejection floods.
- Existing data limits: 1,200-byte unreliable payload; 256-KiB reliable payload;
  256 messages/512 KiB per second per plugin, 1-MiB outbound queue and
  1,024-message/4-MiB inbound queue per plugin. Peer maximum is 51, the session limit of `sco.net`.
- Existing bounded backend pending joins, replay filtering and stale-peer expiry
  remain in force.

Host logging records broker publication/stop, Host/Join result, numeric state
transition and leave reason category. It never records a passphrase, player name
or endpoint. This is lifecycle logging, not a tamper-proof OS security audit or
proof of firewall/private-routing enforcement. The host shell owns and must
independently verify official-network blocking and restoration.

## Verification and packaging

`test_net_control` executes the real authenticated backend with synthetic numeric
IPv4 datagrams and failing socket traps. With `SCO_GAME_SC=ON` it also exercises
the published C service, partial extents, consent/bounds/thread refusal, collision
readiness, owner release, admission loss and request-rate rejection. C ABI pins
are compiled alongside it. Only this named test is needed for local G13 proof:

```sh
cmake -S . -B build-g13 -G Ninja -DSCO_GAME_SC=ON -DSCO_BUILD_TESTS=ON
cmake --build build-g13 --target test_net_control sco_app sco_game_host
ctest --test-dir build-g13 -R '^test_net_control$' --output-on-failure
```

The public header ships in `game-sc`; provider source, host options and ABI
fixture ship in `host-source-sc`. Neutral broker/socket machinery remains in
`core-source`. No new MIT grant, plugin networking API or G2 mutation is added.
**Windows/MSVC, private-LAN operation, EAC/OS inspection, official-network packet
policy, rendering and gameplay are NOT VERIFIED.**
