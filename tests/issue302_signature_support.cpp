#include "sco/signatures.h"

// Row fixtures supply Sig/SigPattern locally. Do not link the live registry:
// these tests inject dependency addresses rather than registering global rows.
namespace sco {
bool SigReady(const char* id) { return Sig(id) != nullptr; }
}
