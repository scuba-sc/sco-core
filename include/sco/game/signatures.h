#pragma once
// Game signature tables. Each feature area has its own table under src/game/; this registers
// them all. Feature code uses resolved results or the typed accessors in sco/game/*.h.

#include "sco/signature_registry.h"

namespace sco::game {

// Registers the complete pack atomically. The caller owns the registry lifecycle;
// a second registration while building returns DuplicateId, and after freeze returns Frozen.
SigRegistrationResult RegisterGameSignatures(SignatureRegistry& registry);

}  // namespace sco::game
