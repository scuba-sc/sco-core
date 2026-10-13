/* SPDX-License-Identifier: GPL-3.0-only */
#include "sc_private_session.h"
#include <stddef.h>
_Static_assert(sizeof(sc_private_session_host_request_v1) == 40, "host v1 layout");
_Static_assert(offsetof(sc_private_session_host_request_v1, player_name) == 24, "host name");
_Static_assert(sizeof(sc_private_session_join_request_v1) == 48, "join v1 layout");
_Static_assert(offsetof(sc_private_session_join_request_v1, passphrase) == 24, "join secret");
_Static_assert(sizeof(sc_private_session_state_v1) == 16, "state layout");
_Static_assert(sizeof(sc_private_session_v1) == 40, "table layout");
_Static_assert(offsetof(sc_private_session_v1, get_state) == 32, "table tail");
