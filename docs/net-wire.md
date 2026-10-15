# sco.net wire format (internal, as built)

What plan PR 4a built for [design section 3.5](design/multiplayer.md#35-session-crypto-d4): the packets, the handshake, the keys and the delivery rules of `sco.net`, in `src/net/` with headers in `include/sco/net/`. Plugins see only the `sco_net_v1` table ([Multiplayer messages](net.md), plan PR 4b). The wire itself is defined once, in **[`include/sc_net.h`](../include/sc_net.h), MIT** (the [interface exception](../LICENSE), like `sc_ipc.h`): a program outside sco-core includes only that file to frame, parse and verify packets, supplying its own HMAC-SHA-256. sco-core's code takes its constants, parser, MAC input, replay window and name rules from it, and `tests/abi_sc_net.c` pins it. Change it only together with `SC_NET_PROTOCOL_VERSION`; there are two exceptions, argued below:
`SC_NET_MAX_PEERS`, the host's admission limit, which no packet field depends on ([Session size](#session-size)),
and the HOST_ROLE control message, which a joiner that does not know it drops without harm ([Host role](#host-role)).

| File | What |
|---|---|
| `sc_net.h` (MIT) | The wire: constants, kinds, header offsets, body layouts, codes, `sc_net_parse`, `sc_net_write_header`, `sc_net_mac_head` / `sc_net_tag` (over a caller-supplied HMAC), `sc_net_tag_equal`, the replay window, name rules. Header-only C11 / C++20, freestanding headers, no sco-core include |
| `sco/net/sha2.h`, `src/net/sha2.c` | SHA-256, HMAC-SHA-256, PBKDF2-HMAC-SHA256, constant-time compare, wipe. Plain C11, written for sco-core |
| `sco/net/wire.h`, `src/net/wire.cpp` | Constants, framing, the hostile-input parser, the MAC, the replay window, bounds-checked reader |
| `sco/net/reliable.h`, `src/net/reliable.cpp` | Reliable streams: fragmentation, ordering, acknowledgements, retransmission |
| `sco/net/transport.h` | `Endpoint` and the abstract datagram `Transport` |
| `sco/net/udp.h`, `src/net/udp_win.cpp`, `udp_posix.cpp` | The UDP `Transport` (4b): Winsock on the real target, BSD sockets for the tests |
| `sco/net/scope.h`, `src/net/scope.cpp` | The LAN rule and the allow-list (4b), applied around every transport by the service |
| `sco/net/session.h`, `src/net/service.cpp` | The service (4b): the network thread, the `sco_net_v1` table, ownership, quotas, delivery on the game thread, session control |
| `sco/net/core.h`, `src/net/core.cpp` | `sco::net::Core`: handshake, links, the channel table, relay, timers; driven by `Pump(nowMs)` |
| `tests/net_mem.h`, `tests/test_net.cpp` | A seeded lossy, duplicating, reordering in-memory network; the tests |

## Packet

All integers little-endian. Every datagram is at most **1,400 bytes** (`kMaxDatagram`, inside a 1,500-byte MTU).

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `SCON` (bytes `53 43 4F 4E`) |
| 4 | 1 | `protocol_version` (1) |
| 5 | 1 | kind: 1 HELLO, 2 CHALLENGE, 3 PROOF, 4 WELCOME, 5 REFUSE, 6 DATA, 7 ACK, 8 PING, 9 BYE |
| 6 | 2 | channel index in the session table (0 = the control channel `sco.net`) |
| 8 | 8 | `sender_peer_id`: who sent this datagram on this link (0 in the handshake) |
| 16 | 8 | `seq`: per sender and link, from 1, never reused (0 in the handshake) |
| 24 | 4 | `body_len`, at most 1,356 (`kMaxBody`) |
| 28 | n | body |
| 28+n | 16 | tag (kinds 6-9 only): HMAC-SHA-256 truncated to 128 bits |

The tag is computed over `protocol_version || kind || u16 len || channel_fqn || sender_peer_id || seq || body_len || body` under the link key (`sc_net_mac_head` writes everything before the body). That is the design's `protocol_version || channel_fqn || sender_peer_id || seq || payload` in the same order, plus the kind and two lengths, so no two different packets share a MAC input (without the kind, a DATA could be replayed as an ACK of the same bytes; without the lengths, name and body boundaries could shift). The wire carries the channel's **index**, but the MAC covers its **full name**, so an index that resolves to another channel fails the check.

**Parser.** `Parse` checks, in order: at least 28 bytes; at most 1,400; magic; version (the header is still returned, so a HELLO from another version can be answered); a known kind; `body_len` at most 1,356; then the exact length `28 + body_len (+ 16)`, shorter (truncated) and longer (trailing bytes) both refused. Nothing is read through a length before it is checked. Body fields are read with `Reader`, which fails sticky on any read past the end. On a link, a datagram is dropped unless, in order: it comes from the link's address with the link's peer id; its channel index is in the table; the tag verifies (constant time); its `seq` is new in the replay window. Only then is the window updated and the body decoded.

**Replay window.** 1,024 packets per link and direction (`kReplayWindow`). `seq` 0 is never valid; a `seq` at or below `highest - 1024` is too old; inside the window each `seq` is accepted once. Retransmissions are new packets with new `seq`s.

## Handshake and keys

Joiner J, host H. Handshake packets carry no tag; their own fields carry the proofs.

| Step | Kind | Body |
|---|---|---|
| J -> H | HELLO | client nonce `cn` (16), u16 + player name, u16 count + (u16 + channel name) for the names that fit; padded to at least 64 bytes |
| H -> J | CHALLENGE | `cn`, host nonce `hn` (16), session salt (16) |
| J -> H | PROOF | `cn`, `hn`, `HMAC(K, "sco.net proof\0" \|\| cn \|\| hn)` |
| H -> J | WELCOME | `cn`, peer id (u64), `HMAC(K, "sco.net welcome\0" \|\| cn \|\| hn \|\| peer id)` |
| H -> J | REFUSE | `cn`, code (1 version, 2 passphrase, 3 full, 4 not admitted, 5 bad hello), host's version, u16 + text |

- **Session key** `K = PBKDF2-HMAC-SHA256(passphrase, salt, kPbkdf2Iters = 200000, 32 bytes)`. The **host picks the salt** once per session (in `Host`), so it runs PBKDF2 once, not once per join attempt; a joiner runs it once per salt. The passphrase never crosses the wire and is wiped once the key exists.
- **The host checks PROOF** in constant time, then asks the product (`Callbacks::admit`), then sends WELCOME. **A wrong passphrase** fails the PROOF check: the host sends REFUSE code 2 and forgets the join; the joiner's state goes to Idle with the reason `refused: wrong passphrase`. A joiner shows its own text for each code, never the unauthenticated host's.
- **The joiner checks WELCOME's MAC**, so a host without the passphrase can't admit anyone (mutual authentication).
- **Link key** `L = HMAC(K, "sco.net link\0" || cn || hn)`, one per join. It authenticates every DATA, ACK, PING and BYE on that link.
- **Robustness.** J resends HELLO, then PROOF, every 500 ms until answered and gives up after 10 s (`no answer from the host`). H answers a repeated HELLO with the same CHALLENGE and a repeated PROOF with the same WELCOME. H keeps at most `2 * SC_NET_MAX_PEERS` (102) half-open joins (the oldest goes first) for 10 s: every seat of a full session can be waiting for its PROOF at once, as when 50 players start in the same moment. HELLO is padded so a CHALLENGE is never larger than the HELLO it answers. A REFUSE must echo J's `cn`, so a blind spoof can't end a join.
- **Version.** A HELLO with another `protocol_version` gets a REFUSE (code 1, carrying the host's version); a joiner that gets a REFUSE with another version reports `refused: protocol version mismatch (host vX, this vY)`. Only the first six header bytes are relied on across versions.

## DATA, ACK, PING, BYE

DATA body:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | flags: 0x1 reliable, 0x2 to-host (other bits refused) |
| 1 | 3 | zero (refused otherwise) |
| 4 | 8 | origin peer id (who wrote the message; differs from the sender when the host relays) |
| 12 | 8 | unit seq (reliable only) |
| 20 | 4 | message length (reliable only, at most 256 KiB) |
| 24 | 4 | this unit's offset in the message (reliable only) |
| 12 / 28 | rest | data: at most 1,200 bytes (an unreliable message, or one unit) |

- **Unreliable:** one datagram, at most 1,200 bytes (`SCO_NET_MAX_UNREL`), may be lost or reordered, never delivered twice (the replay window).
- **Reliable:** at most 256 KiB (`SCO_NET_MAX_RELIABLE`), cut into units of 1,200 bytes, exactly once and in order per channel and link. Each (link, channel, direction) is a stream with its own unit seq. The receiver buffers up to 64 units ahead (`kWindow`) and acknowledges with ACK; the sender keeps at most 64 units in flight, resends a unit after 200 ms, then doubles the timeout up to 2 s, and queues at most 1,024 units per stream (then `Send` answers `QueueFull`). The receiver checks a message's length against **its own** `max_len` for the channel when the first unit is consumed, before reserving anything; a message over it is consumed, acknowledged and dropped (`Stats::refused`). Units whose fields can't belong to any message (a unit over 1,200 bytes, an offset past the length, a length over 256 KiB) are refused before they are buffered.
- **ACK** body: u16 count, then per stream: u16 channel, u16 zero, u64 next expected unit, u64 mask (bit i: unit next + 1 + i received). An ACK can't acknowledge a unit the sender hasn't assigned.
- **PING:** empty, after 1 s with nothing sent on a link. A link silent for 30 s is dropped (`timed out`; a joiner's session ends with `the host timed out`).
- **BYE:** u16 + reason. The host drops the peer (`left: <reason>`); a joiner's session ends (`the host ended the session: <reason>`).

## Sessions, channels and relay

- **Star topology.** Joiners talk only to the host. The host delivers what it registered and relays the rest to every other joiner that registered the channel, under its own link keys, keeping the origin id. TO_HOST messages are not relayed. The host checks that a joiner's origin is the joiner itself.
- **Peer ids.** The host is 1; joiners get 2, 3, ... in join order, never reused in a session (a rejoin gets a new id).
- **The channel table** maps names to indexes and is the host's. A joiner's HELLO names its channels; the rest, and any registered later, go as REGISTER on the control channel. The host assigns indexes (at most 256 entries) and sends the joiner a SYNC (the table and the peers) right after WELCOME (after HOST_ROLE, when the host is a server), then a CHANNEL for every new entry. A joiner can send on a channel once it is in its table.
- **Control channel** (index 0, `sco.net`, reliable, 64 KiB): SYNC (1), CHANNEL (2), REGISTER (3, joiner to host), PEER_JOINED (4), PEER_LEFT (5), HOST_ROLE (6, host to joiner, from a server host only, before SYNC; see [Host role](#host-role)).
- **Receiving.** A message on a channel this endpoint didn't register is counted (`unknownChannel`) and dropped; one over the receiver's `max_len`, or on a FROM_HOST channel from anyone but the host, is counted as `refused` and dropped.

## Session size

A session holds at most `SC_NET_MAX_PEERS` endpoints, the host included: 51. A player host is one of them,
and so is a server host, which takes no player seat ([Host role](#host-role)). The number is the host's own
admission rule, and **the protocol version stays 1** because nothing on the wire depends on it:

- Only the host compares a join with the limit. When the session is full it answers the joiner's PROOF
  with REFUSE code 3 (full). A joiner never learns the limit and never checks it.
- Every id and count is wide enough for any limit. Peer ids are 64-bit and never reused (the header's
  `sender_peer_id`, the DATA origin, WELCOME, PEER_JOINED, PEER_LEFT); the channel index is 16-bit; the ACK
  mask is per reliable unit, not per peer; the HELLO, CHALLENGE, PROOF and WELCOME bodies have fixed sizes.
- The peer list in SYNC has its own u16 count, which a joiner reads without comparing it to any limit.
  The list travels in one SYNC control message on the reliable control stream, fragmented in units of
  `SC_NET_FRAG_BYTES`, never in one datagram. At most 256 channel entries of up to 68 bytes and 51 peers
  of up to 74 bytes make about 21 KB, against the 64 KiB `SC_NET_CONTROL_MAX`.

So a joiner built for 16 seats joins a 51-seat host and reads its peer list like any other, and a host
built for 16 seats keeps refusing at 16, with the same packets on the wire. Only a plugin can notice. The
plugin-facing header keeps `SCO_NET_MAX_PEERS` at 16 (version 1.0 of `sco.net` is frozen) and adds
`SCO_NET_SESSION_MAX_PEERS`, 51, for the runtime's limit; a plugin that sized a `get_peers` buffer by the old
macro gets `SCO_TOO_MANY` (with the first 16 peers written and the true count) once more than 16 players are in
the session ([Limits](net.md#limits)).

## Host role

A host can be a **server**: a headless `sco.net` host that is not a player, run by players (for example a
standalone relay that stays up while the players come and go). It is not the game's own dedicated server
(that model stays excluded, [design section 2](design/multiplayer.md#permanent-exclusions-never-ported)) and
not a relay run by the project. It still is the host and peer 1, because the star topology needs one, but it
takes no player seat and has no ghost, and a client should neither count it nor draw it as a player.
`Options::server` turns it on. The service's own `Host` request and the private-session broker are
unchanged: they always host a player.

**The message.** HOST_ROLE is a control message on channel 0, reliable and authenticated like SYNC: `u8 type`
(6), `u8 flags`. Bit 0 (`SC_NET_HOST_ROLE_F_SERVER`) means server; every other bit must be 0. A server host
sends it to each joiner right after WELCOME and before SYNC, so the joiner knows what the host is before it
hears of any peer, and the host's JOINED event already has the answer. A player host sends nothing, and a
joiner that never gets one reads the host as a player (flags 0). A joiner accepts it once, from the host,
before SYNC, with exactly two bytes and no undefined bit. Anything else is counted as a malformed control
message and changes nothing, so a role is never half applied. A host counts one received from a joiner as
malformed, like any control message but REGISTER. No other body changes: SYNC, PEER_JOINED and PEER_LEFT
carry the same fields as before. `Core::Peers()` marks the host's entry with `kPeerServer` on the host and on
every joiner, and the service answers `get_peer_flags` from it ([Multiplayer messages](net.md#servers-11)).

**Seats and the transport bound.** Two limits are counted apart. *Player seats* are `Options::maxPeers`: a
player host is one of them, so it admits `maxPeers - 1` joiners; a server is none, so it admits `maxPeers`.
The *transport bound* is `SC_NET_MAX_PEERS` endpoints (51), the host's own slot counted whether it plays or
not. A join must fit both, or the joiner gets REFUSE code 3 (full). With 50 seats a player host admits 49
joiners and a server admits 50. With the seat budget at 51 a player host admits 50 and a server still
admits 50, because its own slot is the 51st endpoint. The 52nd endpoint is refused in every case. Peer ids
do not change: the host is 1, joiners get 2, 3, ..., never reused.

**The protocol version stays 1.** A joiner built before this message must not be locked out of a server, and
it is not. What such a joiner does with the message, from the code of the 2.2.0 release (`sdk-v2.2.0`,
where the joiner path in `src/net/` is identical to this code, and the only change under `src/` before this
pull request is the host-side bound on half-open joins):

- A joiner reads a control message in `Core::Impl::OnControl`. The switch over the type has a `default:`
  that does `++stats.malformed; return;`, and type 6 is unknown to it. Nothing on that path ends the
  session, removes the link or stops the stream.
- The unit that carried the message is acknowledged like any other: `RecvStream::OnUnit` sets `ackDue`
  before the message is handed to `OnControl`, whatever it says. The reliable control stream does not
  stall, SYNC arrives behind it in order, and `OnSync` keeps no memory of earlier control messages.
- So a 2.2.0 joiner joins a server, reaches Joined, lists the server as a player (all it can know) and
  counts one malformed message more than against a player host. Every other counter is the same. The cost
  is cosmetic: an old game lists a server as one more player.
- A joiner that knows the message joins a 2.2.0 host, which sends none, and lists it as a player.
- Nothing else moved: framing, handshake, keys, SYNC, ids and the host's limit are as they were.

The alternative is protocol version 2, which makes both sides know HOST_ROLE. A HELLO from a 2.2.0 joiner
then gets REFUSE code 1 (protocol version mismatch), so no game built from 2.2.0 or earlier could join a
server at all, in exchange for no joiner ever listing a server as a player. It is the maintainers' choice.
The change is `SC_NET_PROTOCOL_VERSION` and its pin in `tests/abi_sc_net.c`; HOST_ROLE can then be required
instead of optional.

**What the tests cover** (`tests/test_net.cpp` and `tests/test_net_service.cpp`; in memory, not run in game):
real server and player hosts with joiners (everyone agrees on the role, the joiner knows it when the host's
JOINED event fires, no malformed message on either side, a joiner that moves to a host of the other kind
keeps no memory of the first); the seat counts above with up to 50 joiners and the refused next one; a
scripted host that sends the message twice, late, too short, too long and with an undefined bit, and a
control type the joiner does not know (what a 2.2.0 joiner makes of type 6: one malformed message, and the
SYNC behind it is accepted); and the service reading the role through `get_peer_flags`, including a threaded join paused between the JOINED
event and the snapshot. A real 2.2.0 build
joining a server is not part of these tests.

## The service on top (plan PR 4b)

`Core` is single-threaded: the service's network thread owns it and its socket, and applies queued `register_channel`, `unregister_channel` and `send_channel` calls between `Pump`s. The socket sits behind a `ScopedTransport`, so datagrams from outside the LAN rule are dropped before `Parse` ever sees them. Channel ownership (`<plugin id>.`), quotas and the capability are the service's ([Multiplayer messages](net.md)); the wire doesn't change. Not yet: IPv6 sockets, and the C# and Lua layers.

**Known limits, by design.** Traffic is authenticated but **plaintext** (players use a VPN for confidentiality). A passphrase-HMAC handshake lets someone who records a handshake, or poses as a host, test passphrases offline; PBKDF2's 200,000 iterations slow that down, and a long passphrase is the defence. A spoofed REFUSE (which needs J's nonce, so an on-path attacker) or spoofed handshake traffic can stop a join, not get one in.
