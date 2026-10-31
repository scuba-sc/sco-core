#include "sco/signatures.h"

// Fixtures supply legacy Sig/SigPattern locally; their resolver contexts are
// built in isolated owned registries rather than the live host registry.
namespace sco {
bool SigReady(const char* id) { return Sig(id) != nullptr; }
}
