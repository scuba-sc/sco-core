#pragma once
// The signature registry: every game address the mod uses is one named row.
//
// A row says how to find its address (a unique byte pattern, or a resolver function for the
// cases a pattern can't express, such as a string reference followed by layout checks), and
// which other rows it needs. A SignatureRegistry resolves rows in dependency order and
// publishes results. A row whose dependency failed is Blocked, not tried.
//
// Rows are plain input data; registration copies their text. The same table can be used by
// a host and by tools/sco-sigcheck against an image read from disk.
#include "sco/scan.h"
#include <cstddef>
#include <cstdint>

namespace sco {

enum class SigState : uint8_t { NotRun, Ok, Missing, Ambiguous, Failed, Blocked };
const char* SigStateName(SigState s);

struct SigResult {
    SigState    state   = SigState::NotRun;
    uint8_t*    at      = nullptr;
    int         matches = 0;
    const char* why     = nullptr;   // static string; set for Failed / Blocked
};

// Trusted resolver precondition: bounded read-only image access, no engine/global
// mutation, hook installation, publication, lifecycle calls, retained context or throws.
// Image contains writable pointers: const is not enforcement or a sandbox.
// Exception handling cannot roll back resolver side effects. Keep resolver code live.
class SigResolveContext {
public:
    uint8_t* Sig(const char* id) const noexcept;
private:
    using Lookup = uint8_t* (*)(const void*, const char*) noexcept;
    SigResolveContext(const void* state, Lookup lookup) noexcept : state_(state), lookup_(lookup) {}
    const void* state_;
    Lookup lookup_;
    friend class SignatureRegistry;
};
using SigResolver = SigResult (*)(const Image& img, const SigResolveContext& context);

constexpr int kMaxSigNeeds = 4;

struct SigDef {
    const char* id;                        // "teleport.to_camera": <feature>.<thing>
    const char* pattern;                   // unique .text pattern, or nullptr when `resolve` is set
    int         ripDisp;                   // pattern rows: >0 = result is the RIP target of the operand at
    int         ripSize;                   //   match+ripDisp in an instruction of ripSize bytes; 0 = the match itself
    SigResolver resolve;                   // resolver rows
    const char* needs[kMaxSigNeeds];       // ids this row reads; nullptr-terminated
};

// Results helpers for resolvers.
inline SigResult SigOk(const void* at)            { return { SigState::Ok, const_cast<uint8_t*>(static_cast<const uint8_t*>(at)), 1, nullptr }; }
inline SigResult SigFail(const char* why)         { return { SigState::Failed, nullptr, 0, why }; }
SigResult        SigPattern(const Section& text, const char* pattern);   // Ok / Missing / Ambiguous

// Host convenience functions read the published host snapshot. The returned address
// is borrowed from the image and is valid only for that image's lifetime.
uint8_t* Sig(const char* id);
bool SigReady(const char* id);

// "[core] signatures: 61/64 OK" plus one line per row that isn't Ok.
// verbose = also list the Ok rows.
class SignatureSnapshot;
void LogSignatureReport(const SignatureSnapshot& snapshot, bool verbose);

}  // namespace sco
