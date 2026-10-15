/* SPDX-License-Identifier: GPL-3.0-only
 * Host-only private-session control. No socket/bind/allowlist authority is
 * delegated to plugins. All calls are game-thread only, with a live self.
 * Request pointers are borrowed for the call, copied into host-owned storage.
 * consent must be 1 only following fresh explicit local UI Host/Join activation;
 * never set it from startup, settings replay, a peer message or an automatic retry.
 * Host controls the numeric bind and exact private-address allowlist. Default
 * broker is enabled, loopback-only, and opens no socket until Host/Join.
 * EAC/offline admission is checked at each request and tick; loss leaves session.
 */
#ifndef SC_PRIVATE_SESSION_H
#define SC_PRIVATE_SESSION_H
#include "sco_api.h"
#define SC_PRIVATE_SESSION_NAME "game.private_session"
#define SC_PRIVATE_SESSION_VERSION 0x00010000u
#define SC_PRIVATE_SESSION_MIN_PASSPHRASE 12u
#define SC_PRIVATE_SESSION_MAX_PASSPHRASE 256u
#define SC_PRIVATE_SESSION_MAX_PLAYER_NAME 64u
#define SC_PRIVATE_SESSION_MAX_ADDRESS 15u
/* The largest max_peers a host request may name, the host included: 51, the same number as SC_NET_MAX_PEERS
 * (the session layer refuses a larger one). A runtime without SCO_NET_SESSION_MAX_PEERS (an SDK without
 * this change) stops at SC_PRIVATE_SESSION_MAX_PEERS_2_2 and answers SCO_BAD_ARG above it, so a plugin that
 * asks for more and gets that answer retries with the smaller number. Nothing to do with the policy's allow-list of 1-16 exact
 * peer addresses. */
#define SC_PRIVATE_SESSION_MAX_PEERS 51u
#define SC_PRIVATE_SESSION_MAX_PEERS_2_2 16u
#define SC_PRIVATE_SESSION_DEFAULT_PORT 64091u
#ifdef __cplusplus
extern "C" {
#endif
/* Lengths are bytes, excluding terminators; embedded NUL is invalid. Address
 * is numeric IPv4 only. Strings are UTF-8; no pointer is retained. Unknown tail
 * fields are ignored; callers must supply the complete v1 request extent. */
typedef struct sc_private_session_host_request_v1 {
    uint32_t size;
    uint32_t consent;
    const char* passphrase;
    uint32_t passphrase_len;
    uint32_t max_peers;
    const char* player_name;
    uint32_t player_name_len;
    uint16_t port;
    uint16_t _pad;
} sc_private_session_host_request_v1;
typedef struct sc_private_session_join_request_v1 {
    uint32_t size;
    uint32_t consent;
    const char* address;
    uint32_t address_len;
    uint16_t port;
    uint16_t _pad;
    const char* passphrase;
    uint32_t passphrase_len;
    uint32_t player_name_len;
    const char* player_name;
} sc_private_session_join_request_v1;
#define SC_PRIVATE_SESSION_IDLE 0u
#define SC_PRIVATE_SESSION_HOSTING 1u
#define SC_PRIVATE_SESSION_JOINING 2u
#define SC_PRIVATE_SESSION_CONNECTED 3u
typedef struct sc_private_session_state_v1 {
    uint32_t size;
    uint32_t state;
    uint32_t peer_count;
    uint32_t max_peers; /* configured host limit; 0 for joiner/idle (unknown) */
} sc_private_session_state_v1;
typedef struct sc_private_session_v1 {
    uint32_t size;
    uint32_t _pad;
    /* OK means host operation accepted, not authenticated peer connection.
     * BAD_ARG malformed/bounds; FAILED absent consent; UNAVAILABLE admission
     * or disabled/another owner/session; TOO_MANY request rate limit. */
    sco_result (*request_host)(sco_plugin* self, const sc_private_session_host_request_v1* request);
    sco_result (*request_join)(sco_plugin* self, const sc_private_session_join_request_v1* request);
    /* Only the requesting owner can leave; ordinary owner unload also leaves. */
    sco_result (*leave)(sco_plugin* self);
    sco_result (*get_state)(sc_private_session_state_v1* out);
} sc_private_session_v1;
#ifdef __cplusplus
}
#endif
#endif
