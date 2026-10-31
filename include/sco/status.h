#pragma once
// The one-line status message features report to the player (the menu's status strip).
// Features call Status(); the UI reads GetStatus(). Features never include UI headers.
#include <cstddef>

namespace sco {

void Status(const char* fmt, ...);        // stores the message and logs it as "[status] ..."
// False and out="" until first Status(); reserved ingress refusal returns false
// without changing out. Status itself becomes a no-op after reservation.
bool GetStatus(char* out, size_t n);

}  // namespace sco
